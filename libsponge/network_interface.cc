#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address),
    _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    auto arp_iter = _arp_table.find(next_hop_ip);

    // 如果arp表未命中
    if(arp_iter == _arp_table.end()) {
        _ip_datagram_waiting_to_send.push_back({next_hop, dgram});
        
        // 之前没发送过arp报文
        if(_waiting_ip_mapping_arp.find(next_hop_ip) == _waiting_ip_mapping_arp.end()) {
            ARPMessage arp_req;
            arp_req.opcode = ARPMessage::OPCODE_REQUEST;
            arp_req.sender_ethernet_address = _ethernet_address;
            arp_req.sender_ip_address = _ip_address.ipv4_numeric();
            arp_req.target_ethernet_address = {};
            arp_req.target_ip_address = next_hop_ip;

            EthernetFrame eth_frame;
            eth_frame.header() = {
                ETHERNET_BROADCAST,
                _ethernet_address,
                EthernetHeader::TYPE_ARP
            };
            eth_frame.payload() = arp_req.serialize();
            _frames_out.push(eth_frame);
            _waiting_ip_mapping_arp[next_hop_ip] = _default_arp_wait_response_ttl;
        }
    }


    else {
        EthernetFrame eth_frame;
        eth_frame.header() = {
            arp_iter->second.eth_addr,
            _ethernet_address,
            EthernetHeader::TYPE_IPv4
        };
        eth_frame.payload() = dgram.serialize();
        _frames_out.push(eth_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const auto header = frame.header();
    // 既不是 广播 也不是 发给自己的
    if(header.dst != ETHERNET_BROADCAST && header.dst != _ethernet_address) {
        return nullopt;
    }
    // ip 包
    if(header.type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if(dgram.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }

        return dgram;
    } 

    // arp 报文  ip比对只有arp 请求报文
    else if(header.type == EthernetHeader::TYPE_ARP){
        ARPMessage arp_msg;
        if(arp_msg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        bool is_arp_request = arp_msg.opcode == ARPMessage::OPCODE_REQUEST && arp_msg.target_ip_address == _ip_address.ipv4_numeric();
        bool is_arp_reply = arp_msg.opcode == ARPMessage::OPCODE_REPLY &&  arp_msg.target_ethernet_address == _ethernet_address;
        // arp request
        if(is_arp_request) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
            arp_reply.target_ip_address = arp_msg.sender_ip_address;

            EthernetFrame eth_frame;
            eth_frame.header() = {
                arp_msg.sender_ethernet_address,
                _ethernet_address,
                EthernetHeader::TYPE_ARP
            };
            eth_frame.payload() = arp_reply.serialize();
            _frames_out.push(eth_frame);
        }
        // arp reply or arp request
        if(is_arp_reply || is_arp_request){
            _arp_table[arp_msg.sender_ip_address] = {arp_msg.sender_ethernet_address, _default_arp_talbe_ttl};
            _waiting_ip_mapping_arp.erase(arp_msg.sender_ip_address);

            for(auto iter = _ip_datagram_waiting_to_send.begin(); iter != _ip_datagram_waiting_to_send.end();) {
                if(iter->first.ipv4_numeric() == arp_msg.sender_ip_address) {
                    send_datagram(iter->second, iter->first);
                    
                    iter = _ip_datagram_waiting_to_send.erase(iter);

                } else {
                    iter ++;
                }
            }
        }
    }

    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // 更新arp、表
    for(auto iter = _arp_table.begin(); iter != _arp_table.end();) {
        if(iter->second.ttl <= ms_since_last_tick) {
            iter = _arp_table.erase(iter);
        } else {
            iter->second.ttl -= ms_since_last_tick;
            iter ++;
        }
    }

    // 更新 跟踪的arp报文
    for(auto iter = _waiting_ip_mapping_arp.begin(); iter != _waiting_ip_mapping_arp.end();) {


        if(iter->second <= ms_since_last_tick) {
            ARPMessage arp_req;
            arp_req.opcode = ARPMessage::OPCODE_REQUEST;
            arp_req.sender_ethernet_address = _ethernet_address;
            arp_req.sender_ip_address = _ip_address.ipv4_numeric();
            arp_req.target_ethernet_address = {};
            arp_req.target_ip_address = iter->first;

            EthernetFrame eth_frame;
            eth_frame.header() = {
                ETHERNET_BROADCAST,
                _ethernet_address,
                EthernetHeader::TYPE_ARP
            };
            eth_frame.payload() = arp_req.serialize();
            _frames_out.push(eth_frame);
            iter->second = _default_arp_wait_response_ttl;
        } else {
            iter->second -= ms_since_last_tick;   
            iter ++;         
        }
    }
}
