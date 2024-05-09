#include "byte_stream.hh"
#include <iostream>

// Dummy implementation of a flow-controlled in-memory byte stream.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) 
	: bstream(""), read_byte(0), written_byte(0),
	max_byte(capacity), byte_in_stream(0), in_end(false){}

size_t ByteStream::write(const string &data) {
	int should_len = data.length();
	if(should_len + byte_in_stream > max_byte)
		should_len = max_byte - byte_in_stream;
	/*现在将data加入string中*/
	for(int i = 0; i < should_len; i++)
		bstream += data[i];
	written_byte += should_len;
	byte_in_stream += should_len;
	return should_len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
	size_t ret = (len > byte_in_stream) ? byte_in_stream : len;
	return bstream.substr(0, ret);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) 
{  
	int pop_len = (len > byte_in_stream) ? byte_in_stream : len;
	bstream.erase(0, pop_len);
	read_byte += pop_len;
	byte_in_stream -= pop_len;
	/*
	 *一个很无语的地方错了，是直接在pop的时候改变read_byte的值
	 * */
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
	size_t read_len = (len > byte_in_stream) ? byte_in_stream : len;
	std::string ret = peek_output(read_len);
	pop_output(read_len);
	return ret;
}

void ByteStream::end_input() 
{
	/*设置flag*/
	in_end = true;
}

bool ByteStream::input_ended() const { return in_end; }

size_t ByteStream::buffer_size() const 
{ 
	return byte_in_stream;
}

bool ByteStream::buffer_empty() const 
{ 
	return (byte_in_stream == 0);
}

bool ByteStream::eof() const 
{ 
	/*如果此时没有input，并且全部读完了，那么eof*/
	if(byte_in_stream == 0 && input_ended())
		return true;
	return false;
}

size_t ByteStream::bytes_written() const 
{
    return written_byte;	
}

size_t ByteStream::bytes_read() const 
{
   	return read_byte;
}

size_t ByteStream::remaining_capacity() const
{ 
	return max_byte - byte_in_stream;
}
