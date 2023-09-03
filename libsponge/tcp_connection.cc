#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received_ms; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_received_ms = 0;
    const TCPHeader header = seg.header();

    if(TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_SENT) {
        // SYN_SENT 状态下不接受data
        if(seg.payload().size()) return;
        // SYN_SENT 状态下 收到不含ack的rst 直接忽略
        if(header.rst && !header.ack) return; 
        if(header.ack && header.ackno.raw_value() != _sender.next_seqno().raw_value()) return;  
    }
    
    if(header.rst) {

        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _linger_after_streams_finish = false;
        _active = false;

        return;
    }

    _receiver.segment_received(seg);

    bool send_empty = false;
    if(header.ack) {
        // // 监听状态下收到ack 直接rst
        // if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN &&
        // TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED ) {
        //     TCPSegment s;
        //     s.header().seqno = header.ackno;
        //     s.header().rst = true;
        //     _segments_out.push(s);

        //     _sender.stream_in().set_error();
        //     _receiver.stream_out().set_error();
        //     _linger_after_streams_finish = false;
        //     _active = false;
        //     return;
        // }
        // if a unacceptable ackno, send a ACK back
        if(!_sender.ack_received(header.ackno, header.win)) {
            send_empty = true;
        }
        _add_segment_with_ack_and_win_and_out();
    }

    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN &&
    TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        return;
    }

    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
    TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        _active = true;
        connect();
        _add_segment_with_ack_and_win_and_out();
        return;
    }

    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
    TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }

    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
    TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {

        _active = false;
        return;
    }

    // 如果收到的seg 1.有数据，就要回一个ACK 2. seg中的ackno超过了， 也要发ACK 3. 收到的seqno 与receiver的ackno 不一致
    if((seg.length_in_sequence_space() || send_empty || seg.header().seqno != _receiver.ackno().value()) && _segments_out.empty()) {
        _sender.send_empty_segment();
    }
    _add_segment_with_ack_and_win_and_out();

}


bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    _add_segment_with_ack_and_win_and_out();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received_ms += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    _add_segment_with_ack_and_win_and_out();

    if(_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        TCPSegment seg = segments_out().front();
        segments_out().pop();

        while(!_sender.segments_out().empty()) {
            _sender.segments_out().pop();
        }
        while(!_segments_out.empty()) {
            _segments_out.pop();
        }
        // rst包 是用重传的数据包 置rst = true, 置ack = false;
        seg.header().ack = false;
        seg.header().rst = true;
        _segments_out.push(seg);

        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _linger_after_streams_finish = false;
        _active = false;

        return;
    }


    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && 
        _linger_after_streams_finish && _time_since_last_segment_received_ms >= 10 * _cfg.rt_timeout) {

        _linger_after_streams_finish = false;    
        _active = false;
        return;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    _add_segment_with_ack_and_win_and_out();
}

void TCPConnection::connect() {
    _active = true;
    _sender.fill_window();
    _add_segment_with_ack_and_win_and_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _linger_after_streams_finish = false;
            _active = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::_add_segment_with_ack_and_win_and_out() {
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        _segments_out.push(seg);
    }
}

