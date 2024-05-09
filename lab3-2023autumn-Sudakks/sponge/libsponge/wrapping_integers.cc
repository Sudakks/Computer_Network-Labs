#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
	/*好像没有%的运算操作*/
	/*需要用强制数据转化，然后得到的就是32位的，这样不再需要取模*/
	return static_cast<WrappingInt32>(n) + isn.raw_value();
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
	WrappingInt32 offset(n - isn);
	WrappingInt32 _n(wrap(checkpoint, isn));
	int64_t ans = n - _n + checkpoint;
	if(ans < 0)
		ans = ans + (1ul << 32);
	return ans;
	/*这里有一种特殊情况
	 * 就是当isn = 0, absolute = 1, checkpoint = 0时
	 * n = 0, _n = 2^32 - 1
	 * 那么最终算出来absolute = 1 - 2^32
	 * 但是最终答案应该是1,所以还要在负数情况上+2^32
	 * 并且一直以uint的形式计算，那么检测不了是负数，故需要转换成int来判断
	 * */
}
