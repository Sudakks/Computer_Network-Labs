#include "tcp_connection.hh"
#include <limits>

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check`.
/*问题记录：
* (1)对于server，在收到segment的时候，要传给自己的TCPreceiver
* 然后对于这个包发回ack                                                                                                  
* 但是一开始server的rcv会想发ack和syn，但是由于此时没有可发的包，故不会发回ack，那么此时client接收不到ack
* 即如果rcv的queue为空，那么直接发空包，然后设置syn和ack位置
* (2)是如何发送rst信号，刚开始是想发送一个空包，然后直接添加rst
* 但是发现empty segment不一定在queue的前面，所以打算让rst附加在其他有info的包上面
* 故如果有error，那么添加rst
* (3)由此，unclean的措施也要改，直接按照pdf上说的设定error和改变active
* 即无论是接收还是发送rst，都需要上述unclean的动作，但是发送rst，还需要有sender_to_connection的操作
* (4)_sender.stream_in().end_input()：错误是sender无法发送fin，其实是我自己要控制sender的input ended，刚开始写错了，写成input_ended了……就bool函数……搞错了！！！
* TIME_WAIT状态应该是>=cfg时，就退出，之前写成了>才退出
* (5)对于test34,发送了fin未收到回应，应该重传，此时需要把sender队列的东西再丢入connection再发，这个是在sender的tick里面完成的，所以需要在tick里面重传
* (6)linger变量设置错误，应该是在收到inbound fin的时候更改，所以在segment_received函数时添加clean shutdown
* (7)tick里面：if(!active() || _sender.next_seqno_absolute() == 0)
* 一定要判断是否已经开始syn，否则后面会直接开始syn（还没收到connect信号之前）
* (8)好好笑，不能乱输出，差点一直de了
*/

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPConnection::send_rst()
{
	unclean_shutdown();
	_sender.fill_window();
	if(_sender.segments_out().empty())//即表明自己不对只有ACK的报文ack
		_sender.send_empty_segment();
	sender_to_connection();
}

void TCPConnection::sender_to_connection() 
{
	TCPSegment now;
	bool flag = true;
	while(!_sender.segments_out().empty())
	{
		now = _sender.segments_out().front();
		_sender.segments_out().pop();

		//now.header().win = (_receiver.window_size() > std::numeric_limits<uint16_t>::max()) ? std::numeric_limits<uint16_t>::max() : _receiver.window_size();

		//对于outgoing segment，要设定ackno
		//即要对发出去的包设定ackno
		//现在的身份是receiver
		//receiver=`waiting for SYN: ackno is empty
		if(_receiver.ackno().has_value())
		{
			//设定ackno和flag
			now.header().ack = true;
			now.header().ackno = _receiver.ackno().value();
			now.header().win = _receiver.window_size();
		}
		//这是利用有info的segment发送rst信号
		if((_sender.stream_in().error() || _receiver.stream_out().error()) && flag)
		{
			flag = false;	
			now.header().rst = true;
		}
		_segments_out.push(now);
	}		
}

void TCPConnection::unclean_shutdown()
{
	//只要发送或者接收rst，就unclean shutdown
	_sender.stream_in().set_error();
	_receiver.stream_out().set_error();
	is_active = false;
}

void TCPConnection::clean_shutdown()
{
	/*
	 * 进行clean shutdown的几个要求：
	 * 1. inbound已经全部被组装完毕，且ended了
	 * 2. outbounded已经ended，且全部发出去了
	 * 3. outbounded已经被全部认可了*/

	//如果inbound已经end，但是outbound还没有到达eof，那么设置为false
	//即这个时候不需要等待
	if(inbound_stream().input_ended() && !_sender.stream_in().eof())
		_linger_after_streams_finish = false;

	bool case1 = inbound_stream().input_ended() && (unassembled_bytes() == 0);
	bool case2 = _sender.stream_in().input_ended() && _segments_out.size() == 0;
	bool case3 = bytes_in_flight() == 0;
	if(case1 && case2 && case3)
	{
		if(_linger_after_streams_finish == false)
			is_active = false;
		//这里应该是大于等于！！！
		else if(last_time_received >= 10 * _cfg.rt_timeout)
			is_active = false;
	}
}

size_t TCPConnection::remaining_outbound_capacity() const { 
	return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const { 
	return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const { 
	return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const 
{
	return last_time_received;
}

void TCPConnection::segment_received(const TCPSegment &seg) 
{ 
	if(!active())
		return;
	last_time_received = 0;
	if(!seg.header().syn && _sender.next_seqno_absolute() == 0)
		return;
	/*
	if(seg.header().syn == true && _sender.next_seqno_absolute() == 0)
	{
	_receiver.segment_received(seg);
	_sender.fill_window();
	if(_sender.segments_out().empty() && seg.length_in_sequence_space() > 0)//即表明自己不对只有ACK的报文ack
		_sender.send_empty_segment();
	sender_to_connection();
	clean_shutdown();
	return;
	}
	 if (!seg.header().syn && !_receiver.ackno().has_value()) {
        return;
    }
	*/

	//接收到rst
	if(seg.header().rst)
	{
		unclean_shutdown();
		return;
	}

	if(seg.header().ack)
	{
		_sender.ack_received(seg.header().ackno, seg.header().win);
	}
	_receiver.segment_received(seg);
	_sender.fill_window();
	//把fill_window放在发送空包之前，因为如果fill_window之后，有东西发，那么并不需要发送空包，避免多发空包
	//if the incoming segment occupied any sequence numbers, the TCPConnection makes sure that at least one segment is sent in reply
	if(_sender.segments_out().empty() && seg.length_in_sequence_space() > 0)//即表明自己不对只有ACK的报文ack
		_sender.send_empty_segment();
	sender_to_connection();
	//cout << "SEND state = " << state().state_summary(_sender) << endl;
	//cout << "RECEIVER state = " << state().state_summary(_receiver) << endl;
	clean_shutdown();//有可能接收到inbound ended的消息所以需要及时更新linger
}

bool TCPConnection::active() const 
{
	return is_active;
}

size_t TCPConnection::write(const string &data) {
	if(!active())
		return 0;
	size_t ret = _sender.stream_in().write(data);
	//相当于写到了_sender的buffer里面
//	_sender.fill_window();
	//但实际上还需要调用sender让它真正地写，即从刚刚write进去的里面取出来
	sender_to_connection();
	return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) 
{
	//如果还没开始发syn，后面重传会导致发syn（因为有fill window调用）	
	if(!active() || _sender.next_seqno_absolute() == 0)
		return;

   last_time_received += ms_since_last_tick;	
   _sender.tick(ms_since_last_tick);
   //把这个从重传的后面移到了前面，然后就可以reset的了
	if(_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS)
	{
		send_rst();
	}
	else
		clean_shutdown();

   //好像是sender的tick的问题……我麻了
   _sender.fill_window();
   sender_to_connection();//重传
   //Expectation: exactly one segment sent with (F=1,)
	   //每个tick都在检测此时是否需要clean shutdown
}

void TCPConnection::end_input_stream() 
{
	_sender.stream_in().end_input();
	//因为这个时候已经设置了outbound为eof，那么在fill_window的时候就会检测到，然后自动发送syn
	_sender.fill_window();
	sender_to_connection();
	//结束了，并且要能让它把fin发送出去
}

void TCPConnection::connect() 
{
	//表示开始发送内容，即先要设定syn=1,开始发送
	is_active = true;
	_sender.fill_window();
	sender_to_connection();
	//然后开始将Sender里面的segment丢入Connection
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
			send_rst();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
