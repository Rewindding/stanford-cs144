#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { 
    size_t used=_sender.bytes_in_flight();
    if(used>=_sender.rcv_window_size()) return 0;
    else return _sender.rcv_window_size()-used; 
}

size_t TCPConnection::bytes_in_flight() const { 
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {     
    return _sender.stream_in().buffer_size(); 
}

size_t TCPConnection::time_since_last_segment_received() const { 
    return _time_passed-_time_last_ack_rcvd;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    //handle unclear shutdown
    if(seg.header().rst){
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    //is isn received, handle 3-way handshake here!
    if(!_receiver.syn_received()){
        if(!seg.header().syn) return;
        //receive this seg.
        _receiver.segment_received(seg);
        _sender.send_empty_segment();
        //echo back an ack when syn first received!
        connect();   
        return;
    }
    bool fall_in_window=_receiver.segment_received(seg);
    //out of window bound seg arrived    
    if(!fall_in_window&&_receiver.ackno().has_value()){
        //send ack and window size immidiately to correct the remote sender
        _sender.send_empty_segment();
        TCPSegment& ack_seg=_sender.segments_out().front();
        //why should bare ack seg has a seqno? to make sure it's correctly received not falling out of the receiver's window
        ack_seg.header().ack=true;
        ack_seg.header().ackno=_receiver.ackno().value();
        ack_seg.header().win=_receiver.window_size();
        _segments_out.push(ack_seg);
        _sender.segments_out().pop();
    }

    //update the sender's info at the same time
    if(seg.header().ack()){
        bool ack_rcv = _sender.ack_received(seg.header().ackno,seg.header().win);
        if(!ack_rcv){
            //if ack get wrong what should do to correct the receiver?
            //cannot ack acks!
        }
    }
}

bool TCPConnection::active() const { 
    bool rst=_sender.stream_in().error()||_receiver.stream_out().error();
    return rst||(_sender.stream_in().eof()&&_receiver.stream_out().eof()); 
}

size_t TCPConnection::write(const string &data) {
    size_t writed = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segment();
    return writed;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    _time_passed+=ms_since_last_tick;
    if(_sender.consecutive_retransmissions()>=_cfg.MAX_RETX_ATTEMPTS){
        //shut down connection
        //~TCPConnection();
    }
}

void TCPConnection::end_input_stream() {
    _time_last_ack_rcvd=_time_passed;
    _sender.stream_in().end_input();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_segment();
}

void TCPConnection::send_segment(){
    auto& sender_seg_que=_sender.segments_out();
    while(!sender_seg_que.empty()){
        //put ack at the same time, TCPSender it self did't know TCPReceiver's ackno
        TCPSegment& seg=sender_seg_que.front();
        if(_receiver.ackno().has_value()){//duplicate ack problem?
            seg.header().ack=true;
            seg.header().ackno=_receiver.ackno();
        }
        //put window size for flow control
        seg.header().win=_receiver.window_size();
        _segments_out.push(seg);
        sender_seg_que.pop();
    }
}
TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _sender.send_empty_segment();
            TCPSegment& rst=_sender.segments_out().front();
            rst.header().rst=true;
            send_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
