#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
	, timer()
	, cons_retrans_num(0)
	, current_time(0)
	, RTO{retx_timeout}
	, window_sz(1)
	, _bytes_in_flight(0)
	, has_send_syn(false)
	, has_send_fin(false)
	, recent_ack(0)
	, should_double(true){}
	/*啥呀，我怎么不理解了？？？为什么从false改为true就可以用了*/
	/*是一开始因为size肯定大于0,所以一定要double吗，现调用tick再调用ack_received*/

uint64_t TCPSender::bytes_in_flight() const
{ 
	return _bytes_in_flight;
}

void TCPSender::fill_window() 
{
	// Remember that the SYN and FIN flags also occupy a sequence number each, which means that t_hey occupy space in the window.
	TCPSegment now;
	if(has_send_syn == false && _next_seqno == 0)
	{
		//发送syn不能带上其他数据，要等接收到syn后开始发送数据
		//即第一个segment要带上syn标志
		now.header().syn = true;
		now.header().seqno = wrap(0, _isn);
		has_send_syn = true;
		_bytes_in_flight += 1;
		_next_seqno = 1;
		unacked_segs.push(now);
		_segments_out.push(now);
	}
	else
	{
		window_sz = (window_sz == 0) ? 1 : window_sz;
		//此时选择单独发送fin，过了test16
		while(window_sz > _bytes_in_flight && !has_send_fin/*&& !_stream.buffer_empty()*/)
		{
			//需要注意，不是empty就不发了，很可能需要在下一个包里面单独设定fin，所以还需要一轮
			//每一轮循环都要判断一次，即如果发送了fin，直接退出（但依然要开定时器）
			//加了每一次判断是否发了fin的内容，过了test16,即不能继续发包了
			size_t free_space = window_sz - _bytes_in_flight;
			std::string str = _stream.read(min(TCPConfig::MAX_PAYLOAD_SIZE, min(free_space, _stream.buffer_size())));
			//要把string类型转换成Buffer类型
			now.payload() = Buffer(std::move(str));//使用一个构造函数
			now.header().seqno = next_seqno();//转成了32位

			//Piggyback FIN in segment when space is available
			//如果可以加入fin，直接加入fin，不要放到下一个包里面发送了
			if(!has_send_fin && 1 + now.length_in_sequence_space() <= free_space && _stream.eof())
			{
				has_send_fin = true;
				now.header().fin = true;
				//有可能只单发一个fin包
			}
			size_t len = now.length_in_sequence_space();
			if(len == 0)
				break;//表示什么东西都没有发送
					  //这里是break，因为还要set时钟
					  //需要加，因为有可能buffer_size刚好为0,但是还没input_ended，所以还没发送fin
			_bytes_in_flight += len;
			_next_seqno += len;
			unacked_segs.push(now);
			_segments_out.push(now);
		}
	}
	if(timer.get_is_on() == false)
	{
		//计数器没开，那么现在打开
		timer.set_is_on(true);
		timer.set_last_start_time(current_time);
	}	
	//这个也要包括syn的传输，即一开始传输syn的时候，也需要开定时器
	//加了这个过了test13
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//接受到新的ackno，看现有还没被acked的segment是否在这个区间内，在那么删除，并且重新fill_window
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) 
{
	window_sz = window_size;
	if(window_sz == 0)
		should_double = false;
	else
		should_double = true;
	//不知道在这里直接0+1会不会i影响tick的判断，所以还是放在fill里面好了
	//不能直接将_next_seqno和ackno比较，因为数据类型不统一
	uint64_t abs_ackno = unwrap(ackno, _isn, recent_ack);
	//好吧，这里似乎用_next_seqno也是正确的
	//TCPSender希望接下来收到的内容是他自己的_next_seqno，所以用这个作为checkpoint
	//并且这个ackno要大于base的ack，即unacked中第一个的ackno
	//注意要自己记录recent ack（也就是checkpoint的值）
	if(abs_ackno > _next_seqno || abs_ackno <= recent_ack)
		return;
	/*这里之前没有abs和next的比较，那么很可能发送了一个大于所有unacked_segs里seqno的ack，这样会全部pop
	 * 但是其实不可以，所以需要判断发回来的ack是对所有unacked的seqno进行确认
	 * The TCPSender was in state `stream ongoing`, but it was expected to be in state `stream started but nothing acknowledged`
	 * 上面是错误信息，就是因为发了一个很大的ack导致所有的unacked_segs都弹出来了，使得state在stream ongoing
	 * 但实际上应该是nothing acknowledged
	 * */

	//收到了合法的ack，说明网络拥塞情况得到缓解，那么reset某些值
			//restart timer
	while(unacked_segs.size() > 0)
	{
		//删除ackno<abs_ackno的seqments
		TCPSegment now = unacked_segs.front();
		uint64_t _header_ackno = unwrap(now.header().seqno, _isn, recent_ack);
		recent_ack = abs_ackno;
		//啊啊啊我知道了，我是把seqno和ackno搞混了！！！
		if(_header_ackno + now.length_in_sequence_space() <= abs_ackno)
		{
			//即完整的接收到了一块segment
			unacked_segs.pop();
			_bytes_in_flight -= now.length_in_sequence_space();
		}
		else
			break;
	}
	if(unacked_segs.size() > 0)
	{
		timer.set_last_start_time(current_time);
		timer.set_is_on(true);
	}
	else
		timer.set_is_on(false);
	fill_window();
	RTO = _initial_retransmission_timeout;
	cons_retrans_num = 0;
	//这个应该是收到了比之前大的ack就变化，不管有没有pop出segs来
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) 
{
   //根据现在的时间，调整timer中的一些值	
    current_time += ms_since_last_tick;
	if(timer.get_is_on() == true && current_time - timer.get_last_start_time() >= RTO)
	{//要保证这个timer是开的状态
		//timer expired
		if(unacked_segs.size() != 0)
		{
			TCPSegment retrans_seg = unacked_segs.front();//再重传这个包
			_segments_out.push(retrans_seg);
			//When filling window, treat a '0' window size as equal to '1' but don't back off RTO
			if(window_sz != 0 && should_double)
			{
				cons_retrans_num += 1;
				RTO *= 2;
			}
			timer.set_is_on(true);
			timer.set_last_start_time(current_time);//重启这个timer
		}
	}
}

unsigned int TCPSender::consecutive_retransmissions() const 
{ 
	return cons_retrans_num;
}

/*发送一个空的segment*/
void TCPSender::send_empty_segment() 
{
	TCPSegment empty_segment;
	empty_segment.header().ackno = next_seqno();
	//header是private，所以只能通过内部函数访问
	_segments_out.push(empty_segment);
}



/********************************************/
/*开始TCPTimer*/
TCPTimer::TCPTimer():
	is_on(false),
	last_start_time(0){}


void TCPTimer::set_is_on(bool on)
{
	is_on = on;
}

void TCPTimer::set_last_start_time(unsigned int value)
{
	last_start_time = value;
}

unsigned int TCPTimer::get_last_start_time()
{
	return last_start_time;
}

bool TCPTimer::get_is_on()
{
	return is_on;
}
