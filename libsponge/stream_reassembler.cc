#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) 
    : 
    _unassemble_strs(),
    _next_assembled_idx(0),
    _unassemble_bytes_num(0),
    _eof_idx(-1),
    _output(capacity),
    _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        // eof 指向最后一个字节的下一位
        _eof_idx = index + data.size();
    }

    if(_next_assembled_idx >= _eof_idx) {
        _output.end_input();
        return;
    }

    auto pos_iter = _unassemble_strs.upper_bound(index);
    if (pos_iter != _unassemble_strs.begin() ) pos_iter --;

    size_t new_idx = index;
    
    // index前有未装配的子串
    if (pos_iter != _unassemble_strs.end() && pos_iter->first <= index) {

        size_t end_idx = pos_iter->first + pos_iter->second.size();

        if( end_idx > index){
            new_idx = end_idx;
        }
    }

    else if(index <= _next_assembled_idx) {
        new_idx = _next_assembled_idx;
    }

    // new_idx 越界直接丢弃
    if (new_idx - index >= data.size() ) return;

    pos_iter = _unassemble_strs.lower_bound(new_idx);
    size_t data_size = data.size() - (new_idx - index);

    // 判断后面有没有重叠的
    while (pos_iter != _unassemble_strs.end() && pos_iter->first >= new_idx) {
        if (index + data.size() <= pos_iter->first ) {
            break;
        }
        
        else if(index + data.size() < pos_iter->first + pos_iter->second.size()) {
            data_size = pos_iter->first - new_idx;
            break;
        }

        else {
            _unassemble_bytes_num -= pos_iter->second.size();
            pos_iter =  _unassemble_strs.erase(pos_iter);
        }
    }


    // capicity 指的是 ByteStream + _unassemble_str 的字节数， 可以适当超出
    size_t first_unacceptable_idx = _next_assembled_idx + _capacity - _output.buffer_size();

    // 超出容量直接丢弃
    if (new_idx >= first_unacceptable_idx) {
        return;
    }

    // 开始判断能否装配 和 更新 _unassemble_strs
    if (data_size > 0) {
        // 新的子串
        const string new_data = data.substr(new_idx - index, data_size);

        // 如果可以装配
        if(new_idx == _next_assembled_idx) {

            size_t write_bytes = _output.write(new_data);
            _next_assembled_idx += write_bytes;

            // 读不完的存起来
            if(write_bytes < new_data.size()) {

                const string data_to_store = new_data.substr(write_bytes);
                _unassemble_bytes_num += data_to_store.size();
                _unassemble_strs.insert(make_pair(_next_assembled_idx, data_to_store));
            }
        } else {
            _unassemble_bytes_num += new_data.size();
            _unassemble_strs.insert(make_pair(new_idx, new_data));
        }
    }

    // 处理还能继续装配的情况
    for(auto iter = _unassemble_strs.begin(); iter != _unassemble_strs.end();) {

        if(iter->first == _next_assembled_idx) {
            const size_t write_bytes = _output.write(iter->second);
            _next_assembled_idx += write_bytes;

            // 写不完
            if(write_bytes < iter->second.size()) {
                const string data_to_store = iter->second.substr(write_bytes);

                _unassemble_bytes_num += data_to_store.size();
                _unassemble_strs.insert(make_pair(_next_assembled_idx, data_to_store));
                _unassemble_bytes_num -= iter->second.size();
                _unassemble_strs.erase(iter);
                break;
            }

            else {
                _unassemble_bytes_num -= iter->second.size();
                iter = _unassemble_strs.erase(iter);
            }
        }

        else {
            break;
        }
    }

    if(_next_assembled_idx >= _eof_idx) {
        _output.end_input();
    }


}

size_t StreamReassembler::unassembled_bytes() const { return _unassemble_bytes_num; }

bool StreamReassembler::empty() const { return _unassemble_bytes_num == 0; }
