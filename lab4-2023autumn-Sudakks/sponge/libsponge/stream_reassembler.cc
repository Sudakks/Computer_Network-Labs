#include "stream_reassembler.hh"
#include "byte_stream.hh"

// Dummy implementation of a stream reassembler.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity),
   	buffer(capacity, 0), unass_byte(0), vis(capacity, false), now_index(0), eof_index(0), in_end(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
	if(eof)
	{
		in_end = true;
		eof_index = data.length() + index;
	}
	size_t left = (index >= now_index) ? index : now_index;
	size_t right = (index + data.length() <= now_index + _output.remaining_capacity()) ? index + data.length() : now_index + _output.remaining_capacity(); 
	/*flag this range*/
	for(size_t i = left; i < right; i++)
	{
		size_t tmp = i % _capacity;
		//说明没有在unassembled里面，加进去
		if(vis[tmp] == false)
		{
			unass_byte++;
			vis[tmp]=  true;
			//这个是在完全没有东西的时候，才计数
		}
		buffer[tmp] = data[i - index];
	}
	/*现在将buffer里面的东西拼接进output里*/
	size_t sta = now_index;
	std::string str = "";
	while(vis[sta % _capacity])
	{
		str += buffer[sta % _capacity];
		sta++;
		if(sta >= now_index + _capacity)
			break;
		/*啊是这里，就是可能正好此时可以读一圈下来，所以至多读一圈
		 *但是如果不加这个break条件，那么就有可能一直循环地读
		 *故导致了死循环
		*/
	}
	size_t written_len;
	if(sta > now_index)
	{
		written_len = _output.write(str);//这是实际写入的数量
		for(size_t i = 0; i < written_len; i++)
			vis[(now_index + i) % _capacity] = false;
		unass_byte -= written_len;
		now_index += written_len;
	}
	//需要控制是否output在end了
	if(in_end && now_index == eof_index)
		_output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const { return unass_byte; }

bool StreamReassembler::empty() const { return unass_byte == 0; }
