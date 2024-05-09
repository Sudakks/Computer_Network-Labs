#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
	TCPHeader tHead = seg.header();//获得该TCPSegment的header
	if(tHead.syn == true)
	{
		syn = true;
		isn = tHead.seqno;//获得每次wrap的isn
	}
	//阿啊啊啊啊竟然是这个判断错了，只要接收到过syn，那么就不用return，逻辑判断写错了
	if(syn == false)
		return;//说明还没开始
	if(tHead.fin == true)
		fin = true;

	std::string data = seg.payload().copy();//获得数据
	uint64_t checkpoint = _reassembler.stream_out().bytes_written();//last reassembled byte才是checkpoint
	uint64_t index;
	/*即seqno->absolute seqno->stream index*/
	if(tHead.syn == true)/*好像+1写在里面还是外面都没问题*/
		index = unwrap(tHead.seqno + 1, isn, checkpoint);	
	else
		index = unwrap(tHead.seqno, isn, checkpoint);	
	_reassembler.push_substring(data, index - 1, tHead.fin);
	/*是这里，应该只有当受到fin信号的那一次，才把eof设置为1
	 * 否则一开始就收到eof的字符串，下一次一旦拼接一部分，直接ended input了*/
}

optional<WrappingInt32> TCPReceiver::ackno() const 
{ 
	if(syn == false)
		return std::nullopt;//还没开始，直接返回空
	//通过_reassembler里面已经排序好的位置，得出下一次可以连在一起的序列
	uint64_t written_sz = _reassembler.stream_out().bytes_written();
	uint64_t abs_seqno = written_sz + 1;//stream index -> abs seqno
	/*正常情况是还没读完，那么往组装好的下一个字符开始读
	 * 但是如果end了，即收到了fin，那么还要往fin的后面读一个
	 */
	//eof虽然设置成了true，即在fin到达时设置成了true，但此时可能还没有读完，那么不能把它关掉
	if(_reassembler.stream_out().input_ended())
		abs_seqno++;
	WrappingInt32 _ackno = wrap(abs_seqno, isn);
	return _ackno;
}

size_t TCPReceiver::window_size() const 
{ 
	/*这个是first_unacceptible 到 first_unassemble之间的距离，即已经组装好了，但是还没有读的内容*/
	return _capacity-_reassembler.stream_out().buffer_size();
}
