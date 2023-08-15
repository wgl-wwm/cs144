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
    , _timeOut{retx_timeout}
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    if(_set_fin_flag) {
        return;
    }
    // 窗口大小为0时设为1
    size_t window_size = _last_window_size ? _last_window_size : 1;

    while(window_size > _bytes_in_flight) {
        TCPSegment segment;
        if(!_set_syn_flag) {
            segment.header().syn = true;
            _set_syn_flag = true;
        }

        segment.header().seqno = wrap(_next_seqno, _isn);
        size_t payload_size = min(window_size - _bytes_in_flight + (segment.header().syn ? 1 : 0), TCPConfig::MAX_PAYLOAD_SIZE);
        string payload = _stream.read(payload_size);
        segment.payload() = Buffer(move(payload));

        // 判断字节流是否读完，进而如果字节流结束是否能将fin放入segment
        if(_stream.eof() && payload.size() < payload_size) {
            segment.header().fin = true;
            _set_fin_flag = true;
        }
        
        if(segment.length_in_sequence_space() == 0) {
            break;
        }
        // 更新相关信息， 将segment加入发送队列， 同时跟踪segment的接收情况
        _segments_out.push(segment);
        _segments_map.insert(make_pair(_next_seqno, segment));
        _next_seqno += segment.length_in_sequence_space();
        _bytes_in_flight += segment.length_in_sequence_space();
        // 如果是fin包则退出
        if(_set_fin_flag) break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 如果ackno 是任何TCPSender没有发送过的字节编号， 则返回 false
    size_t abs_ackno = unwrap(ackno, _isn,_next_seqno);

    _last_window_size = window_size;
    _consecutive_retransmissions_count = 0;

    if(_next_seqno >= abs_ackno) {

        _bytes_in_flight = _next_seqno - abs_ackno;
        const auto iter_end = _segments_map.lower_bound(abs_ackno);
        auto iter = _segments_map.begin();

        bool flag = false;
        for(;iter != iter_end;) {
            iter = _segments_map.erase(iter);
            flag = true;
        }

        if(flag) {
            _timer = 0;
            _timeOut = _initial_retransmission_timeout;
        }
    }
    fill_window();
    if(_next_seqno < abs_ackno) return false;
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _timer += ms_since_last_tick;

    if(!_segments_map.empty() && _timer >= _timeOut) {
        auto earliest_segment = _segments_map.begin()->second;

        if(_last_window_size > 0) {
            _timeOut *= 2;
        }
        _timer = 0;
        _segments_out.push(earliest_segment);
        _consecutive_retransmissions_count ++;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions_count; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(segment);
}
