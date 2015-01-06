nginx-log-zmq
=============

ZeroMQ logger module for nginx.

[ZeroMQ](http://zeromq.org), \zero-em-queue\, is a protocol for messages exchange. It's a easy
way to communicate using any language or platform via inproc, IPC, TCP, TPIC or multicast.
It's asynchronous and only requires a small library.

_This module is not distributed with the nginx source._ See the [installation instructions](#installation).

Table of Contents
=================

* [Status](#status)
* [Description](#description)
* [Synopsis](#synopsis)
* [Directives](#directives)
	* [brokerlog_server](#brokerlog_server)
	* [brokerlog_endpoint](#brokerlog_endpoint)
	* [brokerlog_format](#brokerlog_format)
	* [brokerlog_off](#brokerlog_off)
* [Installation](#installation)
* [Compatibility](#compatibility)
* [Report Bugs](#report-bugs)
* [Authors](#authors)
* [Copyright & Licence](#copyright--licence)

Status
======

This module is already production ready.

Description
===========

This is a nginx logger module integrated with [ZeroMQ](http://zermq.org) library.

`nginx-log-zmq` provides a very efficient way to log data for one or more PUB/SUB subscribers, over one or more different endpoints. This can be useful for data gathering and processing.

The message format can be the same as the tradicional log format which gives a interesting way to `tail` data via the network or exploring other text formats like JSON. As with the traditional log, it's possible to use nginx variables updated each request.

All messages are sent asynchronously and do not block the normal behaviour of the nginx server. As expected, the connections are resilient to network failures.

Synopsis
========

```
	http {

		server {
			# simple message to an IPC endpoint with 4 threads and 1000 queue elements

			brokerlog_server main "/tmp/main.ipc" ipc 4 1000;
			brokerlog_endpoint  main "/topic/";

			brokerlog_format main '{"remote_addr":"$remote_addr"}'

			# send messages to a subscriber listening at 127.0.0.1:5556

			brokerlog_server secondary 127.0.0.1:5556 tcp 4 1000;

			location /status {
				# mute all messages from brokerlog for this location

				brokerlog_off all;
			}

			location /endpoint {

				# mute main messages from brokerlog for this location
				brokerlog_off main;

				# set secondary endpoint for this location
				brokerlog_endpoint secondary "/endpoint/";

				# set format using multiline

				brokerlog_format secondary '{"request_uri":"$request_uri",'
										   '{"status":"$status"}';
			}
		}
	}
```

Directives
==========

brokerlog_server
----------------
**syntax:** *brokerlog_server &lt;definition_name&gt; &lt;address&gt; &lt;ipc|tcp&gt; &lt;threads&gt; &lt;queue size&gt;*

**default:** no

**context:** server, location

Configures a server (PUB/SUB subscriber) to connect to.

The following options are required:

**definition_name** &lt;name&gt; - the name that nginx will use to identify this logger instance.

**address** &lt;path&gt;|&lt;ipaddress&gt;:&lt;port&gt; - the subscriber's address. If you are using the IPC
protocol, you should specify the `<path>` for the unix socket. If you are using the TCP
protocol, you should specify the `<ipaddress>` and `<port>` where your ZeroMQ subscriber is listening.

**protocol** &lt;ipc|tcp&gt; - the protocol to be used for communication.

**threads** &lt;integer&gt; - the number of I/O threads to be used.

**queue_size** &lt;integer&gt; - the maximum queue size for messages waiting to be sent.

[Back to TOC](#table-of-contents)

brokerlog_endpoint
------------------

**syntax:** *brokerlog_endpoint &lt;definition_name&gt; "&lt;topic&gt;"*

**default:** no

**context:** server, location

Configures the topic for the ZeroMQ messages.

**definition_name** &lt;name&gt; - the name that nginx will use to identify this logger instance.

**topic** &lt;topic&gt; - the topic for the messages. This is a string (which can be a nginx variable) prepended to every sent message. For example, if you send the message "hello" to the "/talk:" topic, the message will end up as "/talk:hello".

Example:

```
server {
	brokerlog_server main "/tmp/example.ipc" 4 1000;

	# send a message for for an topic based on response status
	brokerlog_endpoint main "/remote/$status";
}
```

[Back to TOC](#table-of-contents)

brokerlog_format
----------------

**syntax:** *brokerlog_format &lt;definition_name&gt; "&lt;format&gt;"*

**default:** no

**context:** server, location

Configures the ZeroMQ message format.

**definition_name** &lt;name&gt; - the name that nginx will use to identify this logger instance.

**format** &lt;format&gt; - the format for the messages. This defines the actual messages sent to the PUB/SUB subscriber. It follows the sames rules as the standard `log_format` directive. It is possible to use nginx variables here, and also to break it over multiple lines.

```
server {
	brokerlog_format main '{"line1": value,'
                          '{"line2": value}';
}
```

[Back to TOC](#table-of-contents)

brokerlog_off
-------------

**syntax:** *brokerlog_off &lt;definition_name&gt;|all*

**default:** no

**context:** server, location

Turn off ZeroMQ logging in the current context.

**definition_name** &lt;name&gt; the name of the logger instance to be muted. If the special `all` name is used, all logger instances are muted.

[Back to TOC](#table-of-contents)

Installation
============

To build a nginx binary containting this module:

* Download the latest version of this module from [GitHub](http://github.com/danielfbento/nginx-log-zmq/).
* Grab the nginx source code from [nginx.org](http://www.nginx.org), for example, version 1.6.2 (see [nginx compatibility](#compatibility)), and then build it like so:

```
./configure --prefix=/usr/local/nginx --add-module=/path/to/nginx-log-zmq

make
make install
```

[Back to TOC](#table-of-contents)

Compatibility
===========

The following versions of nginx are known to work with this module:

* 1.6.x (last tested: 1.6.2)
* 1.5.x
* 1.4.x (last tested: 1.4.4)

[Back to TOC](#table-of-contents)

Report Bugs
===========

Bug reports, wishlists, or patches are welcome. You can submit them on our [GitHub repository](http://github.com/danielfbento/nginx-log-zmq/).

[Back to TOC](#table-of-contents)

Authors
=======

 * Dani Bento &lt;dani@telecom.pt&gt;

[Back to TOC](#table-of-contents)

Copyright & Licence
===================

The MIT License (MIT)

Copyright (c) 2014 SAPO - PT Comunicações S.A

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

[Back to TOC](#table-of-contents)
