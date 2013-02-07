
/***************************************************************************
 *  peer.cpp - Protobuf stream protocol - broadcast peer
 *
 *  Created: Mon Feb 04 17:19:17 2013
 *  Copyright  2013  Tim Niemueller [www.niemueller.de]
 ****************************************************************************/

/*  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * - Neither the name of the authors nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <protobuf_comm/peer.h>

#include <boost/lexical_cast.hpp>

using namespace boost::asio;
using namespace boost::system;

namespace protobuf_comm {
#if 0 /* just to make Emacs auto-indent happy */
}
#endif

/** @class ProtobufBroadcastPeer <protobuf_comm/peer.h>
 * Communicate by broadcasting protobuf messages.
 * This class allows to communicate via UDP by broadcasting messages to the
 * network.
 * @author Tim Niemueller
 */

/** Constructor.
 * @param address IPv4 broadcast address to send to
 * @param port IPv4 UDP port to listen on and to send to
 */ 
ProtobufBroadcastPeer::ProtobufBroadcastPeer(const std::string address, unsigned short port)
  : io_service_(), resolver_(io_service_),
    socket_(io_service_, ip::udp::endpoint(ip::udp::v4(), port))
{
  in_data_size_ = max_packet_length;
  in_data_ = malloc(in_data_size_);

  socket_.set_option(socket_base::broadcast(true));

  outbound_active_ = true;
  ip::udp::resolver::query query(address, boost::lexical_cast<std::string>(port));
  resolver_.async_resolve(query,
			  boost::bind(&ProtobufBroadcastPeer::handle_resolve, this,
				      boost::asio::placeholders::error,
				      boost::asio::placeholders::iterator));

  start_recv();
  asio_thread_ = std::thread(&ProtobufBroadcastPeer::run_asio, this);
}


/** Testing constructor.
 * This constructor listens and sends to different ports. It can be used to
 * send and receive on the same host or even from within the same process.
 * It is most useful for communication tests.
 * @param address IPv4 address to send to
 * @param send_to_port IPv4 UDP port to send data to
 * @param recv_on_port IPv4 UDP port to receive data on
 */
ProtobufBroadcastPeer::ProtobufBroadcastPeer(const std::string address,
					     unsigned short send_to_port,
					     unsigned short recv_on_port)
  : io_service_(), resolver_(io_service_),
    socket_(io_service_, ip::udp::endpoint(ip::udp::v4(), recv_on_port))
{
  in_data_size_ = max_packet_length;
  in_data_ = malloc(in_data_size_);

  socket_.set_option(socket_base::broadcast(true));
  socket_.set_option(socket_base::reuse_address(true));

  outbound_active_ = true;
  ip::udp::resolver::query query(address, boost::lexical_cast<std::string>(send_to_port));
  resolver_.async_resolve(query,
			  boost::bind(&ProtobufBroadcastPeer::handle_resolve, this,
				      boost::asio::placeholders::error,
				      boost::asio::placeholders::iterator));

  start_recv();
  asio_thread_ = std::thread(&ProtobufBroadcastPeer::run_asio, this);
}


/** Destructor. */
ProtobufBroadcastPeer::~ProtobufBroadcastPeer()
{
  if (asio_thread_.joinable()) {
    io_service_.stop();
    asio_thread_.join();
  }
  free(in_data_);
}


/** ASIO thread runnable. */
void
ProtobufBroadcastPeer::run_asio()
{
  while (! io_service_.stopped()) {
    usleep(0);
    io_service_.reset();
    io_service_.run();
  }
}


void
ProtobufBroadcastPeer::handle_resolve(const boost::system::error_code& err,
				     ip::udp::resolver::iterator endpoint_iterator)
{
  if (! err) {
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_active_   = false;
    outbound_endpoint_ = endpoint_iterator->endpoint();
  } else {
    sig_error_(err);
  }
  start_send();
}

void
ProtobufBroadcastPeer::handle_recv(const boost::system::error_code& error,
				   size_t bytes_rcvd)
{
  if (!error && bytes_rcvd >= sizeof(frame_header_t)) {
    frame_header_t *frame_header = static_cast<frame_header_t *>(in_data_);
    size_t to_read = ntohl(frame_header->payload_size);

    if (bytes_rcvd == (sizeof(frame_header_t) + to_read)) {
      uint16_t comp_id   = ntohs(frame_header->component_id);
      uint16_t msg_type  = ntohs(frame_header->msg_type);
      void *msg_data = (char *)in_data_ + sizeof(frame_header_t);
      std::shared_ptr<google::protobuf::Message> m =
	message_register_.deserialize(*frame_header, msg_data);

      sig_rcvd_(in_endpoint_, comp_id, msg_type, m);
    } else {
      sig_error_(errc::make_error_code(errc::illegal_byte_sequence));
    }

  } else {
    sig_error_(error);
  }

  start_recv();
}


void
ProtobufBroadcastPeer::handle_sent(const boost::system::error_code& error,
				   size_t bytes_transferred, QueueEntry *entry)
{
  delete entry;

  {
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_active_ = false;
  }

  if (error) {
    sig_error_(error);
  }

  start_send();
}


/** Send a message to the server.
 * @param component_id ID of the component to address
 * @param msg_type numeric message type
 * @param m message to send
 */
void
ProtobufBroadcastPeer::send(uint16_t component_id, uint16_t msg_type,
			    google::protobuf::Message &m)
{
  QueueEntry *entry = new QueueEntry();
  message_register_.serialize(component_id, msg_type, m,
			      entry->frame_header, entry->serialized_message);

  if (entry->serialized_message.size() > max_packet_length) {
    throw std::runtime_error("Serialized message too big");
  }
  entry->buffers[0] = boost::asio::buffer(&entry->frame_header, sizeof(frame_header_t));
  entry->buffers[1] = boost::asio::buffer(entry->serialized_message);
 
  {
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_queue_.push(entry);
  }
  start_send();
}

void
ProtobufBroadcastPeer::start_recv()
{
  socket_.async_receive_from(boost::asio::buffer(in_data_, in_data_size_),
			     in_endpoint_,
			     boost::bind(&ProtobufBroadcastPeer::handle_recv,
					 this, boost::asio::placeholders::error,
					 boost::asio::placeholders::bytes_transferred));
}

void
ProtobufBroadcastPeer::start_send()
{
  std::lock_guard<std::mutex> lock(outbound_mutex_);
  if (outbound_queue_.empty() || outbound_active_)  return;

  outbound_active_ = true;

  QueueEntry *entry = outbound_queue_.front();
  outbound_queue_.pop();

  socket_.async_send_to(entry->buffers, outbound_endpoint_,
			boost::bind(&ProtobufBroadcastPeer::handle_sent, this,
				    boost::asio::placeholders::error,
				    boost::asio::placeholders::bytes_transferred,
				    entry));
}


} // end namespace protobuf_comm
