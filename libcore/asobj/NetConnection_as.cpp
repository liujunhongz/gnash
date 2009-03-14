// NetConnection_as.cpp:  Open local connections for FLV files or URLs.
// 
//   Copyright (C) 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//


#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include <iostream>
#include <string>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include "NetConnection_as.h"
#include "log.h"
#include "GnashException.h"
#include "builtin_function.h"
#include "movie_root.h"
#include "Object.h" // for getObjectInterface

#include "StreamProvider.h"
#include "URLAccessManager.h"
#include "URL.h"

// for NetConnection_as.call()
#include "VM.h"
#include "amf.h"
#include "http.h"
#include "SimpleBuffer.h"
#include "amf_msg.h"
#include "buffer.h"
#include "namedStrings.h"
#include "element.h"
#include "network.h"
#include "rtmp.h"
#include "rtmp_client.h"

using namespace std;

#define GNASH_DEBUG_REMOTING 1

// Forward declarations.

namespace gnash {

namespace {
    void attachProperties(as_object& o);
    void attachNetConnectionInterface(as_object& o);
    as_object* getNetConnectionInterface();
    as_value netconnection_isConnected(const fn_call& fn);
    as_value netconnection_uri(const fn_call& fn);
    as_value netconnection_connect(const fn_call& fn);
    as_value netconnection_close(const fn_call& fn);
    as_value netconnection_call(const fn_call& fn);
    as_value netconnection_addHeader(const fn_call& fn);
    as_value netconnection_new(const fn_call& fn);

}

//----- NetConnection_as ----------------------------------------------------

NetConnection_as::NetConnection_as()
    :
    as_object(getNetConnectionInterface()),
    _uri(),
    _isConnected(false)
{
    attachProperties(*this);
}

// extern (used by Global.cpp)
void
netconnection_class_init(as_object& global)
{
    // This is going to be the global NetConnection "class"/"function"
    static boost::intrusive_ptr<builtin_function> cl;

    if (cl == NULL) {
        cl=new builtin_function(&netconnection_new,
                getNetConnectionInterface());
        // replicate all interface to class, to be able to access
        // all methods as static functions
        attachNetConnectionInterface(*cl);
             
    }

    // Register _global.String
    global.init_member("NetConnection", cl.get());
}

// here to have HTTPRemotingHandler definition available
NetConnection_as::~NetConnection_as()
{
}

void
NetConnection_as::markReachableResources() const
{
}


/// FIXME: this should not use _uri, but rather take a URL argument.
/// Validation should probably be done on connect() only and return a 
/// bool indicating validity. That can be used to return a failure
/// for invalid or blocked URLs.
std::string
NetConnection_as::validateURL() const
{

    const movie_root& mr = _vm.getRoot();
    URL uri(_uri, mr.runInfo().baseURL());

    std::string uriStr(uri.str());
    assert(uriStr.find("://") != std::string::npos);

    // Check if we're allowed to open url
    if (!URLAccessManager::allow(uri)) {
        log_security(_("Gnash is not allowed to open this url: %s"), uriStr);
        return "";
    }

    log_debug(_("Connection to movie: %s"), uriStr);

    return uriStr;
}

void
NetConnection_as::notifyStatus(StatusCode code)
{
    GNASH_REPORT_FUNCTION;
    std::pair<std::string, std::string> info;
    getStatusCodeInfo(code, info);

    /// This is a new normal object each time (see NetConnection.as)
    as_object* o = new as_object(getObjectInterface());

    const int flags = 0;

    o->init_member("code", info.first, flags);
    o->init_member("level", info.second, flags);

    callMethod(NSV::PROP_ON_STATUS, o);

}

void
NetConnection_as::getStatusCodeInfo(StatusCode code, NetConnectionStatus& info)
{
    /// The Call statuses do exist, but this implementation is a guess.
    switch (code)
    {
        case CONNECT_SUCCESS:
            info.first = "NetConnection.Connect.Success";
            info.second = "status";
            return;

        case CONNECT_FAILED:
            info.first = "NetConnection.Connect.Failed";
            info.second = "error";
            return;

        case CONNECT_APPSHUTDOWN:
            info.first = "NetConnection.Connect.AppShutdown";
            info.second = "error";
            return;

        case CONNECT_REJECTED:
            info.first = "NetConnection.Connect.Rejected";
            info.second = "error";
            return;

        case CALL_FAILED:
            info.first = "NetConnection.Call.Failed";
            info.second = "error";
            return;

        case CALL_BADVERSION:
            info.first = "NetConnection.Call.BadVersion";
            info.second = "status";
            return;

        case CONNECT_CLOSED:
            info.first = "NetConnection.Connect.Closed";
            info.second = "status";
    }

}


/// Called on NetConnection.connect(null).
//
/// The status notification happens immediately, isConnected becomes true.
void
NetConnection_as::connect()
{
    GNASH_REPORT_FUNCTION;
    // Close any current connections.
    close();
    _isConnected = true;
    notifyStatus(CONNECT_SUCCESS);
}


void
NetConnection_as::connect(const std::string& uri)
{
    GNASH_REPORT_FUNCTION;
    // Close any current connections. (why?) Because that's what happens.
    close();

    // TODO: check for other kind of invalidities here...
    if (uri.empty()) {
        _isConnected = false;
        notifyStatus(CONNECT_FAILED);
        return;
    }

    const movie_root& mr = _vm.getRoot();
    URL url(uri, mr.runInfo().baseURL());

    // This is for remoting
    if (!URLAccessManager::allow(url)) {
        log_security(_("Gnash is not allowed to NetConnection.connect to %s"), url);
        notifyStatus(CONNECT_FAILED);
        return;
    }

    _isConnected = false;
}


/// FIXME: This should close an active connection as well as setting the
/// appropriate properties.
void
NetConnection_as::close()
{
    GNASH_REPORT_FUNCTION;

    /// TODO: what should actually happen here? Should an attached
    /// NetStream object be interrupted?
    _isConnected = false;

    notifyStatus(CONNECT_CLOSED);
}


void
NetConnection_as::setURI(const std::string& uri)
{
    init_readonly_property("uri", &netconnection_uri);
    _uri = uri;
}

//
void
NetConnection_as::call(as_object* asCallback, const std::string& methodName,
        const std::vector<as_value>& args, size_t firstArg)
{
    GNASH_REPORT_FUNCTION;

    URL url(_uri);

    // The values for the connect call were set in ::connect(), but according
    // to documentation, the connection isn't actually started till the first
    // ()call(). My guess is back in the days of non-persistant network
    // connections, each ::call() made it's own connection.
    if (_isConnected == false) {

	// We're using RTMPT, which is AMF over HTTP
	short port = strtol(url.port().c_str(), NULL, 0) & 0xffff;
	if ((url.protocol() == "rtmpt")
	    || (url.protocol() == "http")) {
	    _http_client.reset(new HTTP);
	    _http_client->toggleDebug(true);
	    if (!_http_client->createClient(url.hostname(), port)) {
		log_error("Can't connect to server %s on port %s",
			  url.hostname(), url.port());
		notifyStatus(CONNECT_FAILED);
		return;
	    } else {
		log_debug("Connected to server %s on port %s",
			  url.hostname(), url.port());
		notifyStatus(CONNECT_SUCCESS);
		_isConnected = true;
	    }
	// We're using RTMP, Connect via RTMP
	} else if (url.protocol() == "rtmp") {
	    _rtmp_client.reset(new RTMPClient);
	    _rtmp_client->toggleDebug(true);
// 	    if (!_rtmp_client.createClient(url.hostname(), port)) {
// 		log_error("Can't connect to RTMP server %s", url.hostname());
// 		notifyStatus(CONNECT_FAILED);
// 		return;
// 	    }
	    if (!_rtmp_client->handShakeRequest()) {
		log_error("RTMP handshake request failed");
		notifyStatus(CONNECT_FAILED);
		return;
	    }
	    
	    if (!_rtmp_client->clientFinish()) {
		log_error("RTMP handshake completion failed");
		notifyStatus(CONNECT_FAILED);
		return;
	    }
	    string app;		// the application name
	    string path;	// the path to the file on the server
	    string tcUrl;	// the tcUrl field
	    string swfUrl;	// the swfUrl field
	    string filename;	// the filename to play
	    string pageUrl;     // the pageUrl field
	    tcUrl = url.protocol() + "://" + url.hostname();
	    if (!url.querystring().empty()) {
		tcUrl += "/" + url.querystring();
	    } else {
		tcUrl += "/" + url.path();
	    }
	    app = url.path();
	    // FIXME: this should be the name of the refering swf file,
	    // although the value appears to be ignored by the server.
	    swfUrl = "mediaplayer.swf";
	    // FIXME: This should be the URL for the referring web page
	    // although the value appears to be ignored by the server.
	    pageUrl = "http://gnashdev.org";

	    // FIXME: replace the "magic numbers" with intelligently
	    // designed ones.
	    boost::shared_ptr<amf::Buffer> buf2 = _rtmp_client->encodeConnect(app.c_str(), swfUrl.c_str(), tcUrl.c_str(), 615, 124, 1, pageUrl.c_str());
 	    size_t total_size = buf2->allocated();
//   	    _rtmp_client->sendMsg(0x3, RTMP::HEADER_12, total_size, RTMP::INVOKE, RTMPMsg::FROM_CLIENT, *buf2);
//   	    RTMPMsg *msg1 = _rtmp_client->recvMsg();

	    // the connectino process is complete
// 	    if (msg1->getStatus() ==  RTMPMsg::NC_CONNECT_SUCCESS) {
// 		notifyStatus(CONNECT_SUCCESS);
// 	    } else {
// 		notifyStatus(CONNECT_FAILED);
// 		return;
// 	    }
	} // end of 'if RTMP'
#if 0
	// FIXME: do a GET request for the crossdomain.xml file
	// in a portable way!
	log_debug("Requesting crossdomain.xml file...");
	amf::Buffer &request = _http_client->formatRequest("/crossdomain.xml", HTTP::HTTP_GET);
	_http_client->writeNet(request);
#endif
    }
    NetConnection_as::thread_params_t tdata;
    tdata.callback = asCallback;

    static int numCalls = 0;
    amf::AMF_msg top;
    
    boost::shared_ptr<amf::Element> name(new amf::Element);
    name->makeString(methodName);

    // make the result
    std::ostringstream os;
    os << "/";
    // Call number is not used if the callback is undefined
    if ( asCallback ) {
        os << ++numCalls; 
    }
    boost::shared_ptr<amf::Element> response(new amf::Element);
    name->makeString(os.str());

    boost::shared_ptr<amf::Element> data(new amf::Element);
    data->makeStrictArray();
    for (size_t i=firstArg; i<args.size(); i++) {
	cerr << "FIXME: " << args[i].to_string() << endl;
	boost::shared_ptr<amf::Element> el = args[i].to_element();
	el->dump();
	data->addProperty(el);
    }
//     data->dump();

    boost::shared_ptr<amf::AMF_msg::amf_message_t> msg(new amf::AMF_msg::amf_message_t);
    msg->header.target = methodName;
    msg->header.response = os.str();
    msg->header.size = data->calculateSize(*data);
    msg->data = data;
    top.addMessage(msg);
    
    boost::shared_ptr<amf::Buffer> buf = top.encodeAMFPacket();
//     top.dump();
//     buf->dump();

    // Send the request via HTTP
    if ((url.protocol() == "rtmpt")
	|| (url.protocol() == "http")) {
	HTTP http;
	log_debug("Requesting echo response file...");
	// "/echo/gateway"
	amf::Buffer &request = http.formatRequest(url.path(), HTTP::HTTP_POST);
	http.formatContentLength(buf->allocated());
	// All HTTP messages are followed by a blank line.
	http.terminateHeader();
	request += buf;
	_http_client->writeNet(request);
	tdata.network = reinterpret_cast<Network *>(_http_client.get());
	//     tdata.vm = &vm;
    }

    // Send the request via RTMP
    if (url.protocol() == "rtmp") {
	tdata.network = reinterpret_cast<Network *>(_http_client.get());
// 	boost::shared_ptr<buf3> = _rtmp_client->encodeStream(0x2);
// 	//    buf3->dump();
// 	total_size = buf3->size();
// 	RTMPMsg *msg2 = _rtmp_client->sendRecvMsg(0x3, RTMP::HEADER_12, total_size, RTMP::INVOKE, RTMPMsg::FROM_CLIENT, buf3);
    }


    //    this->test();

    // Start a thread to wait for the response
#if 0
    boost::thread process_thread(boost::bind(&net_handler, &tdata));
#else
    net_handler(&tdata);
#endif
    
//    _currentConnection.reset(new HTTPRemotingHandler(*this, url));

}

std::auto_ptr<IOChannel>
NetConnection_as::getStream(const std::string& name)
{
    const RunInfo& ri = _vm.getRoot().runInfo();

    const StreamProvider& streamProvider = ri.streamProvider();

    // Construct URL with base URL (assuming not connected to RTMP server..)
    // TODO: For RTMP return the named stream from an existing RTMP connection.
    // If name is a full or relative URL passed from NetStream.play(), it
    // must be constructed against the base URL, not the NetConnection uri,
    // which should always be null in this case.
    const URL url(name, ri.baseURL());

    const RcInitFile& rcfile = RcInitFile::getDefaultInstance();

    return streamProvider.getStream(url, rcfile.saveStreamingMedia());

}

/// Anonymous namespace for NetConnection interface implementation.

namespace {


/// NetConnection.call()
//
/// Documented to return void, and current tests suggest this might be
/// correct, though they don't test with any calls that might succeed.
as_value
netconnection_call(const fn_call& fn)
{
    GNASH_REPORT_FUNCTION;
    boost::intrusive_ptr<NetConnection_as> ptr = 
        ensureType<NetConnection_as>(fn.this_ptr); 

    if (fn.nargs < 1)
    {
        IF_VERBOSE_ASCODING_ERRORS(
            log_aserror(_("NetConnection.call(): needs at least one argument"));
        );
        return as_value(); 
    }

    const as_value& methodName_as = fn.arg(0);
    std::string methodName = methodName_as.to_string();

#ifdef GNASH_DEBUG_REMOTING
    std::stringstream ss; fn.dump_args(ss);
    log_debug("NetConnection.call(%s)", ss.str());
#endif

    // TODO: arg(1) is the response object. let it know when data comes back
    boost::intrusive_ptr<as_object> asCallback;
    if (fn.nargs > 1) {

        if (fn.arg(1).is_object()) {
            asCallback = (fn.arg(1).to_object());
        }
        else {
            IF_VERBOSE_ASCODING_ERRORS(
                std::stringstream ss; fn.dump_args(ss);
                log_aserror("NetConnection.call(%s): second argument must be "
                    "an object", ss.str());
            );
        }
    }

    const std::vector<as_value>& args = fn.getArgs();
    ptr->call(asCallback.get(), methodName, args, 2);

    return as_value();
}

as_value
netconnection_close(const fn_call& fn)
{
    GNASH_REPORT_FUNCTION;
    boost::intrusive_ptr<NetConnection_as> ptr =
        ensureType<NetConnection_as>(fn.this_ptr); 

    ptr->close();

    return as_value();
}


/// Read-only
as_value
netconnection_isConnected(const fn_call& fn)
{
    boost::intrusive_ptr<NetConnection_as> ptr =
        ensureType<NetConnection_as>(fn.this_ptr); 

    return as_value(ptr->isConnected());
}

as_value
netconnection_uri(const fn_call& fn)
{
    boost::intrusive_ptr<NetConnection_as> ptr =
        ensureType<NetConnection_as>(fn.this_ptr); 

    return as_value(ptr->getURI());
}

void
attachNetConnectionInterface(as_object& o)
{
    o.init_member("connect", new builtin_function(netconnection_connect));
    o.init_member("addHeader", new builtin_function(netconnection_addHeader));
    o.init_member("call", new builtin_function(netconnection_call));
    o.init_member("close", new builtin_function(netconnection_close));
}

void
attachProperties(as_object& o)
{
    o.init_readonly_property("isConnected", &netconnection_isConnected);
}

as_object*
getNetConnectionInterface()
{

    static boost::intrusive_ptr<as_object> o;
    if ( o == NULL )
    {
        o = new as_object(getObjectInterface());
        attachNetConnectionInterface(*o);
    }

    return o.get();
}

/// \brief callback to instantiate a new NetConnection object.
/// \param fn the parameters from the Flash movie
/// \return nothing from the function call.
/// \note The return value is returned through the fn.result member.
as_value
netconnection_new(const fn_call& /* fn */)
{
    GNASH_REPORT_FUNCTION;

    NetConnection_as* nc = new NetConnection_as;

    return as_value(nc);
}


/// For rtmp, NetConnect.connect() takes an RTMP URL. For all other streams,
/// it takes null or undefined.
//
/// RTMP is untested.
//
/// For non-rtmp streams:
//
/// Returns undefined if there are no arguments, true if the first
/// argument is null, otherwise the result of the attempted connection.
/// Undefined is also a valid argument for SWF7 and above.
//
/// The isConnected property is set to the result of connect().
as_value
netconnection_connect(const fn_call& fn)
{
    GNASH_REPORT_FUNCTION;

    boost::intrusive_ptr<NetConnection_as> ptr =
        ensureType<NetConnection_as>(fn.this_ptr); 
    
    if (fn.nargs < 1) {
        IF_VERBOSE_ASCODING_ERRORS(
            log_aserror(_("NetConnection.connect(): needs at least "
                    "one argument"));
        );
        return as_value();
    }

    const as_value& uri = fn.arg(0);

    const VM& vm = ptr->getVM();
    const std::string& uriStr = uri.to_string_versioned(vm.getSWFVersion());
    
    // This is always set without validification.
    ptr->setURI(uriStr);

    // Check first arg for validity 
    if (uri.is_null() || (vm.getSWFVersion() > 6 && uri.is_undefined())) {
        ptr->connect();
    } else {
        if ( fn.nargs > 1 ){
            std::stringstream ss; fn.dump_args(ss);
            log_unimpl("NetConnection.connect(%s): args after the first are "
                    "not supported", ss.str());
        }
        ptr->connect(uriStr);
    }

    return as_value(ptr->isConnected());

}

// This thread waits for data from the server, and executes the callback
extern "C" {
bool DSOEXPORT 
net_handler(NetConnection_as::thread_params_t *args)
{
    GNASH_REPORT_FUNCTION;

#ifdef USE_STATISTICS
	struct timespec start;
	clock_gettime (CLOCK_REALTIME, &start);
#endif
    bool result = false;
    bool done = false;
    int ret = 0;

//    boost::mutex::scoped_lock lock(call_mutex);

//    args->caller->test();

    // Suck all the data waiting for us in the network
    boost::shared_ptr<amf::Buffer> buf(new amf::Buffer);
    do {
	int ret = args->network->readNet(buf->reference() + buf->allocated(), 
					 buf->size(), 10);
	// The timeout expired
 	if (ret == 0) {
	    log_debug("no data yet for fd #%d, continuing...",
		      args->network->getFileFd());
	    result = false;
 	    done = true;
 	}
	// Something happened to the network connection
	if ((ret == static_cast<int>(string::npos)) || (ret == -1)) {
	    log_debug("socket for fd #%d was closed...",
		      args->network->getFileFd());
	    return false;
	}
	// We got data.
	if (ret > 0) {
	    // If we got less data than we tried to read, then we got the
	    // whole packet most likely.
	    if (ret < buf->size()) {
		done = true;
		result = true;
	    }
	    if (ret == buf->size()) {
		// become larger by another default block size.
		buf->resize(buf->size() + amf::NETBUFSIZE);
		log_debug("Got a full packet, making the buffer larger to %d",
			  buf->size());
		result = true;
	    }
	    // manually set the seek pointer in the buffer, as we read
	    // the data into the raw memory allocated to the buffer. We
	    // only want to do this if we got data of course.
	    buf->setSeekPointer(buf->end() + ret);
	} else {
	    log_debug("no more data for fd #%d, exiting...", 
		      args->network->getFileFd());
	    done = true;
	}
    } while(done != true);

    // Now process the data
    if (result) {
	HTTP http;
// 	http.dump();
	amf::AMF amf;
	boost::uint8_t *data = http.processHeaderFields(*buf);
	boost::shared_ptr<std::vector<std::string> > encoding = http.getFieldItem("transfer-encoding");
	size_t length = http.getContentLength();
	if (http.getField("content-type") == "application/x-amf") {
	    amf::AMF_msg amsg;
	    boost::shared_ptr<amf::AMF_msg::context_header_t> head =
		amsg.parseAMFPacket(data, length);
	    
 	    amsg.dump();
	    log_debug("%d messages in AMF packet", amsg.messageCount());
	    for (size_t i=0; i<amsg.messageCount(); i++) {
 		amsg.getMessage(i)->data->dump();
 		boost::shared_ptr<amf::Element> el = amsg.getMessage(i)->data;
 		as_value tmp(*el);
 		NetConnection_as *obj = (NetConnection_as *)args->network;
		log_debug("Calling NetConnection %s(%s)",
			  amsg.getMessage(i)->header.target, tmp);
		// The method name looks something like this: /17/onResult
		// the first field is a sequence number so each response can
		// be matched to the request that made it. We only want the
		// name part, so we can call the method.
		string::size_type pos = amsg.getMessage(i)->header.target.find('/', 1);
		string methodName;
		if (pos != string::npos) {
		    methodName = amsg.getMessage(i)->header.target.substr(pos+1,  amsg.getMessage(i)->header.target.size());
		}
		VM& vm = args->callback->getVM();
 		string_table& st = vm.getStringTable();
		string_table::key methodKey;
		methodKey = st.find(methodName);
  		args->callback->callMethod(methodKey, tmp);
	    }
	} else {// not AMF data
	    log_debug("Content type is: %d", http.getField("content-type"));
	    if ((http.getField("content-type") == "application/xml")
		|| (http.getField("content-type") == "text/html")) {
		log_debug("Textual Data is: %s", reinterpret_cast<char *>(data));
	    } else {
		log_debug("Binary Data is: %s", hexify(data, length, true));
	    }
	}
    }

    log_debug("net_handler all done...");

    return result;
}
} // end of extern C

as_value
netconnection_addHeader(const fn_call& fn)
{
    boost::intrusive_ptr<NetConnection_as> ptr =
        ensureType<NetConnection_as>(fn.this_ptr); 
    UNUSED(ptr);

    log_unimpl("NetConnection.addHeader()");
    return as_value();
}

} // anonymous namespace

} // end of gnash namespace

// local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
