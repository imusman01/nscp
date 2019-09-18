/*
 * Copyright (C) 2004-2016 Michael Medin
 *
 * This file is part of NSClient++ - https://nsclient.org
 *
 * NSClient++ is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * NSClient++ is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NSClient++.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <socket/socket_helpers.hpp>
#include <socket/clients/http/http_packet.hpp>

#include <str/xtos.hpp>

#include <boost/asio.hpp>
#include <boost/version.hpp>
#include <boost/scoped_ptr.hpp>


#include <iostream>
#include <istream>
#include <ostream>
#include <string>



using boost::asio::ip::tcp;

namespace http {

	struct generic_socket {
		typedef boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> tcp_iterator;

		virtual ~generic_socket() {}
		virtual void connect(std::string server_name, std::string port) = 0;
		virtual void write(boost::asio::streambuf &buffer) = 0;
		virtual void read_until(boost::asio::streambuf &buffer, std::string until) = 0;
		virtual bool is_open() const = 0;
		virtual std::size_t read_some(boost::asio::streambuf &buffer, boost::system::error_code &error) = 0;

	};

	struct tcp_socket : public generic_socket {

		tcp::socket socket_;
		tcp::resolver resolver_;


		tcp_socket(boost::asio::io_service &io_service)
			: socket_(io_service)
			, resolver_(io_service)
		{}
		virtual ~tcp_socket() {
			try {
				socket_.close();
			} catch (...) {

			}
		}


		virtual void connect_tcp(const tcp_iterator &endpoint_iterator, std::string server_name, boost::system::error_code &error) {
			socket_.close();
			socket_.connect(endpoint_iterator, error);
		}

		void connect(std::string server, std::string port) {
			tcp::resolver::query query(server, port);
			tcp::resolver::iterator endpoint_iterator = resolver_.resolve(query);
			tcp::resolver::iterator end;

			boost::system::error_code error = boost::asio::error::host_not_found;
			while (error && endpoint_iterator != end) {
				this->connect_tcp(*endpoint_iterator, server, error);
				endpoint_iterator++;
			}
			if (error) {
				throw socket_helpers::socket_exception("Failed to connect to " + server + ":" + port + ": " + error.message());
			}

		}

		void write(boost::asio::streambuf &buffer) {
			boost::asio::write(socket_, buffer);
		}
		void read_until(boost::asio::streambuf &buffer, std::string until) {
			boost::asio::read_until(socket_, buffer, until);
		}
		bool is_open() const {
			return socket_.is_open();
		}
		std::size_t read_some(boost::asio::streambuf &buffer, boost::system::error_code &error) {
			return boost::asio::read(socket_, buffer, boost::asio::transfer_at_least(1), error);
		}

	};

#ifdef USE_SSL

	struct ssl_socket : public generic_socket {

		boost::asio::ssl::context context_;
		boost::asio::ssl::stream<tcp::socket> ssl_socket_;
		tcp::resolver resolver_;

		ssl_socket(boost::asio::io_service &io_service)
#if BOOST_VERSION >= 106800
			: context_(boost::asio::ssl::context::tlsv1)
#else
			: context_(io_service, boost::asio::ssl::context::tlsv1)
#endif
			, ssl_socket_(io_service, context_)
			, resolver_(io_service)
		{
			context_.set_verify_mode(boost::asio::ssl::context::verify_none);
		}

		virtual ~ssl_socket() {
			ssl_socket_.lowest_layer().close();
		}


		void connect_tcp(const tcp_iterator &endpoint_iterator, std::string server_name, boost::system::error_code &error) {
			ssl_socket_.lowest_layer().close();
			ssl_socket_.lowest_layer().connect(endpoint_iterator, error);

			if (error) {
				return;
			}

			if (!server_name.empty()) {

#if BOOST_VERSION >= 104700
				SSL_set_tlsext_host_name(ssl_socket_.native_handle(), server_name.c_str());
#else
				SSL_set_tlsext_host_name(reinterpret_cast<SSL*>(ssl_socket_.impl()), server_name.c_str());
#endif
			}

			ssl_socket_.handshake(boost::asio::ssl::stream_base::client, error);
			if (error) {
				return;
			}
		}

		void connect(std::string server, std::string port) {
			tcp::resolver::query query(server, port);
			tcp::resolver::iterator endpoint_iterator = resolver_.resolve(query);
			tcp::resolver::iterator end;

			boost::system::error_code error = boost::asio::error::host_not_found;
			while (error && endpoint_iterator != end) {
				this->connect_tcp(*endpoint_iterator, server, error);
				endpoint_iterator++;
			}
			if (error) {
				throw socket_helpers::socket_exception("Failed to connect to " + server + ":" + port + ": " + error.message());
			}

		}

		
		void write(boost::asio::streambuf &buffer) {
			boost::asio::write(ssl_socket_, buffer);
		}
		void read_until(boost::asio::streambuf &buffer, std::string until) {
			boost::asio::read_until(ssl_socket_, buffer, until);
		}
		bool is_open() const {
			return ssl_socket_.lowest_layer().is_open();
		}
		std::size_t read_some(boost::asio::streambuf &buffer, boost::system::error_code &error) {
			return boost::asio::read(ssl_socket_, buffer, boost::asio::transfer_at_least(1), error);
		}
	};
#endif

	struct file_socket : public generic_socket {

		boost::asio::windows::stream_handle handle_;

		file_socket(boost::asio::io_service& io_service)
			: handle_(io_service)
		{}
		virtual ~file_socket() {
			try {
				handle_.close();
			}
			catch (...) {

			}
		}


		void connect(std::string pipe_name, std::string port) {
			HANDLE hPipe = ::CreateFileA(
				pipe_name.c_str(), GENERIC_READ|GENERIC_WRITE, 0,
				nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,
				nullptr);

			if (hPipe == INVALID_HANDLE_VALUE) {
				throw socket_helpers::socket_exception("Failed to open pipe " + pipe_name);
			}

			// assign the pipe to our handle
			handle_.assign(hPipe);
		}
		void write(boost::asio::streambuf& buffer) {
			boost::asio::write(handle_, buffer);
		}
		void read_until(boost::asio::streambuf& buffer, std::string until) {
			boost::asio::read_until(handle_, buffer, until);
		}
		bool is_open() const {
			return handle_.is_open();
		}
		std::size_t read_some(boost::asio::streambuf& buffer, boost::system::error_code& error) {
			return boost::asio::read(handle_, buffer, boost::asio::transfer_at_least(1), error);
		}

	};



	class simple_client {
		boost::asio::io_service io_service_;
		boost::scoped_ptr<generic_socket> socket_;
	public:
		simple_client(std::string protocol)
			: io_service_()
		{
			if (protocol == "https") {
#ifdef USE_SSL
				socket_.reset(new ssl_socket(io_service_));
#else
				throw socket_helpers::socket_exception("SSL not supported");
#endif
			} else if (protocol == "pipe") {
				socket_.reset(new file_socket(io_service_));
			} else {
				socket_.reset(new tcp_socket(io_service_));
			}
		}

		~simple_client() {
			socket_.reset();

		}

		void connect(std::string server, std::string port) {
			socket_->connect(server, port);
		}

		void send_request(const http::packet &request) {
			boost::asio::streambuf requestbuf;
			std::ostream request_stream(&requestbuf);
			request.build_request(request_stream);
			socket_->write(requestbuf);
		}
		http::response read_result(boost::asio::streambuf &response_buffer) {
			;

			std::string http_version, status_message;
			unsigned int status_code;
			socket_->read_until(response_buffer, "\r\n");

			std::istream response_stream(&response_buffer);
			if (!response_stream)
				throw socket_helpers::socket_exception("Invalid response");
			response_stream >> http_version;
			response_stream >> status_code;
			std::getline(response_stream, status_message);


			http::response ret(http_version, status_code, status_message);

			if (ret.http_version_.substr(0, 5) != "HTTP/")
				throw socket_helpers::socket_exception("Invalid response: " + ret.http_version_);

			try {
				socket_->read_until(response_buffer, "\r\n\r\n");
			} catch (const std::exception &e) {
				throw socket_helpers::socket_exception(std::string("Failed to read header: ") + e.what());
			}

			std::string header;
			while (std::getline(response_stream, header) && header != "\r")
				ret.add_header(header);
			return ret;

		}

		http::response execute(std::ostream &os, const std::string server, const std::string port, const http::packet &request) {
			connect(server, port);
			send_request(request);

			boost::asio::streambuf response_buffer;
			http::response response = read_result(response_buffer);

			if (!response.is_2xx()) {
				throw socket_helpers::socket_exception("Failed to " + request.verb_ + " " + server + ":" + str::xtos(port) +  " " + str::xtos(response.status_code_) + ": " + response.status_message_);
			}
			if (response_buffer.size() > 0)
				os << &response_buffer;

			boost::system::error_code error;
			if (socket_->is_open()) {
				while (socket_->read_some(response_buffer, error)) {
					os << &response_buffer;
				}
			}

			return response;
		}

		static bool download(std::string protocol, std::string server, std::string port, std::string path, std::ostream &os, std::string &error_msg) {
			try {
				http::packet rq("GET", server, path);
				rq.add_default_headers();
				simple_client c(protocol);
				c.execute(os, server, port, rq);
				return true;
			} catch (const socket_helpers::socket_exception& e) {
				error_msg = e.reason();
				return false;
			} catch (const std::exception& e) {
				error_msg = std::string("Exception: ") + utf8::utf8_from_native(e.what());
				return false;
			}
		}
	};
}
