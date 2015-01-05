nginx-log-zmq
=============

ZeroMQ logger module for nginx.

[ZeroMQ](http://zeromq.org), \zero-em-queue\, is a protocol for messages exchange. It's a easy
way to communicate using any language or platform via inproc, IPC, TCP, TPIC or multicast.
It's asynchronous and requires only a small library.

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

`nginx-log-zmq` provides a very efficient way to log data for one or more subscribers.
Without lose any advantage of ZeroMQ protocol it's possible to send information for
different endpoints/subscribers at the same time. This can be useful for data consuming and processing.

The format of the messages can be the same as the tradicional log format which gives a interesting way to `tail` data via network or you can explore other text formats like JSON. As in the tradicional log, it's possible to use nginx variables which are updated each request.

All messages are sent asynchronously and do not block the normal behaviour of the nginx server. As excepted, the connection to the the subscribers are resilient to network fails.

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
**syntax:** *brokerlog_server &lt;definition_name&gt; &lt;target&gt; &lt;protocol&gt; &lt;threads&gt; &lt;queue size&gt;*

**syntax:** *brokerlog_server &lt;definition_name&gt; &lt;ip&gt;:&lt;port&gt; tcp &lt;threads&gt; &lt;queue size&gt;*

**syntax:** *brokerlog_server &lt;definition_name&gt; "&lt;endpoint&gt;" ipc &lt;threads&gt; &lt;queue size&gt;*

**default:** no

**context:** server, location

Configures a client subscriber to connect.

The following options are required:

**definition_name** &lt;name&gt; the name that nginx will use to identify the logger

**target** "&lt;unixsocket&gt;"|&lt;ip&gt;:&lt;port&gt; the target subscriber. If you are using IPC or INPROC
protocols you should specify the target of the unixsocket. Otherwise, if you are using TCP
protocol you should specify the `<ip>` and `<port>` which your ZMQ client is listening.

**protocol** ipc|tcp|inproc the protocol to be used for communication. IPC uses a path to an unix socket which is defined by `target`. For TCP ip and port needs to specified.

**threads** &lt;num&gt; number of threads to be used for each ZeroMQ context.

**queue_size** &lt;num&gt; the size of the queue used to maintain messages waiting to be sent.

[Back to TOC](#table-of-contents)

brokerlog_endpoint
------------------

**syntax:** *brokerlog_endpoint &lt;definition_name&gt; "&lt;endpoint&gt;"*

**default:** no

**context:** server, location

Configures the endpoint of the ZMQ message

**definition_name** &lt;name&gt; the name that nginx will use to identify the logger

**endpoint** &lt;endpoint&gt; the endpoint of the messages. This represents the endpoint prepended to
the ZMQ message to be used in subscription options. If you send the message "hello" to the endpoint
"/talk/", the message produced by the module will be "/talk/hello".

It's possible to use nginx variables inside the endpoint string:

```
server {
	brokerlog_server main "/tmp/example.ipc" 4 1000;

	# send a message for for an endpoint based on response status
	brokerlog_endpoint main "/remote/$status";
}
```

[Back to TOC](#table-of-contents)

brokerlog_format
----------------

**syntax:** *brokerlog_format &lt;definition_name&gt; "&lt;format&gt;"*

**default:** no

**context:** server, location

Configures the format of the ZMQ message.

**definition_name** &lt;name&gt; the name that nginx will use to identify the logger

**format** &lt;format&gt; the format of the messages. This is the actual message sent to the broker.
It follows the sames rules as the log_format directive. It's possible to use nginx variables
inside it and it's possible to declare the format in multiline.

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

Turn off ZMQ logging.

**definition_name** &lt;name&gt; the name of the logger to be muted. If `all` is used, all loggers are muted in the current location.

[Back to TOC](#table-of-contents)

Installation
============

You can compile this module with nginx core's source by hand:

* Download the lasted version of the release tarball of this module from nginx-log-zmq [file list](http://github.com/danielfbento/nginx-log-zmq/tags).
* Grab the nginx source code from [nginx.org](http://www.nginx.org), for example, the version 1.6.2 (see [nginx compatibility](#compatibility)), and then build the source with this module:

```
wget 'http://nginx.org/download/nginx-1.6.2.tar.gz'
tar -xvzf nginx-1.6.2.tar.gz
cd nginx-1.6.2/

./configure --prefix=/usr/local/nginx \
			--add-module=/path/to/nginx-log-zmq

make -j2
make install
```

[Back to TOC](#table-of-contents)

Compatibility
===========

The following versions of nginx should work with this module:

* 1.6.x (last tested: 1.6.2)
* 1.5.x
* 1.4.x (last tested: 1.4.4)

[Back to TOC](#table-of-contents)

Report Bugs
===========

Please submit bug reports, wishlists, or patches by

1. creating a ticket on the [issue tracking interface](http://github.com/danielfbento/nginx-log-zmq/issues) provided by GitHub

[Back to TOC](#table-of-contents)

Authors
=======

 * Daniel Bento &lt;daniel-s-bento@telecom.pt&gt;

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
