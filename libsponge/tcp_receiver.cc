#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();

    if(header.syn) {
        // 判断有没有收到过syn
        if(_set_syn_flag) return false;

        _set_syn_flag = true;
        _isn = header.seqno;

        if(seg.length_in_sequence_space() == 1) return true;
        if(header.fin) {
            _set_fin_flag = true;
            _reassembler.push_substring(seg.payload().copy(), 0, header.fin);
            return true;
        }

    } else if(!_set_syn_flag) {
        return false;
    } 


    if (header.fin) {
        if (_set_fin_flag || !_set_syn_flag) {
            return false;
        }
        _set_fin_flag = true;
        if(seg.length_in_sequence_space() == 1) {
            _reassembler.push_substring(seg.payload().copy(), unwrap(header.seqno, _isn, _reassembler.stream_out().bytes_written() + 1) - 1, header.fin);
            return true;
        }
    }
    uint64_t ack = _reassembler.stream_out().bytes_written() + 1;
    uint64_t stream_idx = unwrap(header.seqno, _isn, ack) - 1 + (header.syn);

    uint64_t next_unassembled = _reassembler.stream_out().bytes_written() + 1;
    uint64_t unassembled_bytes = _reassembler.unassembled_bytes();

    _reassembler.push_substring(seg.payload().copy(), stream_idx, header.fin);

    // 根据next_unassemled 和 bytes_written 前后有无变化来判断子串是否被插入
    // 需要注意的是 空串是被认为可插入的， 因此需要特判
    if(next_unassembled == _reassembler.stream_out().bytes_written() + 1 && unassembled_bytes == _reassembler.unassembled_bytes()) {
        return false;
    } else {
        return true;
    }
   
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(!_set_syn_flag) {
        return nullopt;
    }
    uint64_t ack = _reassembler.stream_out().bytes_written() + 1;
    if(_reassembler.stream_out().input_ended()) ack++; // fin 占一个字节
    return WrappingInt32(_isn) + ack;
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size();}
