/* neutronServer.cpp
 *
 * Copyright (c) 2014 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Kay Kasemir
 */
#include <iostream>

#include <epicsThread.h>
#include <pv/epicsException.h>
#include <pv/createRequest.h>
#include <pv/event.h>
#include <pv/pvData.h>
#include <pv/clientFactory.h>
#include <pv/pvAccess.h>

using namespace std;
using namespace std::tr1;
using namespace epics::pvData;
using namespace epics::pvAccess;

// -- Requester Helper -----------------------------------------------------------------

static void messageHelper(Requester &requester, string const & message, MessageType messageType)
{
    cout << requester.getRequesterName()
         << " message (" << getMessageTypeName(messageType) << "): "
         << message << endl;
}

// -- ChannelRequester -----------------------------------------------------------------

class MyChannelRequester : public ChannelRequester
{
public:
    string getRequesterName()
    {   return "MyChannelRequester";  }
    void message(string const & message,MessageType messageType)
    {   messageHelper(*this, message, messageType); }

    void channelCreated(const epics::pvData::Status& status, Channel::shared_pointer const & channel);
    void channelStateChange(Channel::shared_pointer const & channel, Channel::ConnectionState connectionState);

    boolean waitUntilConnected(double timeOut)
    {
        return connect_event.wait(timeOut);
    }

private:
    Event connect_event;
};

void MyChannelRequester::channelCreated(const epics::pvData::Status& status, Channel::shared_pointer const & channel)
{
    cout << channel->getChannelName() << " created, " << status << endl;
}

void MyChannelRequester::channelStateChange(Channel::shared_pointer const & channel, Channel::ConnectionState connectionState)
{
    cout << channel->getChannelName() << " state: "
         << Channel::ConnectionStateNames[connectionState]
         << " (" << connectionState << ")" << endl;
    if (connectionState == Channel::CONNECTED)
        connect_event.signal();
}

// -- GetFieldRequester -----------------------------------------------------------------
class MyFieldRequester : public GetFieldRequester
{
public:
    string getRequesterName()
    {   return "MyFieldRequester";  }
    void message(string const & message,MessageType messageType)
    {   messageHelper(*this, message, messageType); }

    void getDone(const Status& status, FieldConstPtr const & field);

    boolean waitUntilDone(double timeOut)
    {
        return done_event.wait(timeOut);
    }

private:
    Event done_event;
};

void MyFieldRequester::getDone(const Status& status, FieldConstPtr const & field)
{
    cout << "Field type: " << field->getType() << endl;
    done_event.signal();
}

// -- ChannelGetRequester -----------------------------------------------------------------
class MyChannelGetRequester : public ChannelGetRequester
{
public:
    string getRequesterName()
    {   return "MyChannelGetRequester";  }
    void message(string const & message,MessageType messageType)
    {   messageHelper(*this, message, messageType); }

    void channelGetConnect(const epics::pvData::Status& status,
            ChannelGet::shared_pointer const & channelGet,
            epics::pvData::Structure::const_shared_pointer const & structure);
    void getDone(const epics::pvData::Status& status,
            ChannelGet::shared_pointer const & channelGet,
            epics::pvData::PVStructure::shared_pointer const & pvStructure,
            epics::pvData::BitSet::shared_pointer const & bitSet);

    boolean waitUntilDone(double timeOut)
    {
        return done_event.wait(timeOut);
    }

private:
    Event done_event;
};


void MyChannelGetRequester::channelGetConnect(const epics::pvData::Status& status,
        ChannelGet::shared_pointer const & channelGet,
        epics::pvData::Structure::const_shared_pointer const & structure)
{
    // Could inspect or memorize the channel's structure...
    if (status.isSuccess())
    {
        cout << "ChannelGet for " << channelGet->getChannel()->getChannelName()
             << " connected, " << status << endl;
        cout << "Channel structure:" << endl;
        structure->dump(cout);

        channelGet->lastRequest();
        channelGet->get();
    }
    else
        cout << "ChannelGet for " << channelGet->getChannel()->getChannelName()
             << " problem, " << status << endl;
    done_event.signal();
}

void MyChannelGetRequester::getDone(const epics::pvData::Status& status,
        ChannelGet::shared_pointer const & channelGet,
        epics::pvData::PVStructure::shared_pointer const & pvStructure,
        epics::pvData::BitSet::shared_pointer const & bitSet)
{
    cout << "ChannelGet for " << channelGet->getChannel()->getChannelName()
         << " finished, " << status << endl;

    if (status.isSuccess())
        pvStructure->dumpValue(cout);
}

// -- Stuff -----------------------------------------------------------------

void monitor(string const &name, string const &request, double timeout)
{
    ChannelProvider::shared_pointer channelProvider =
            getChannelProviderRegistry()->getProvider("pva");
    if (! channelProvider)
        THROW_EXCEPTION2( std::runtime_error, "No channel provider");

    shared_ptr<MyChannelRequester> channelRequester(new MyChannelRequester());
    shared_ptr<Channel> channel(channelProvider->createChannel(name, channelRequester));
    channelRequester->waitUntilConnected(timeout);

    shared_ptr<MyFieldRequester> fieldRequester(new MyFieldRequester());
    channel->getField(fieldRequester, "");
    fieldRequester->waitUntilDone(timeout);

    shared_ptr<MyChannelGetRequester> channelGetRequester(new MyChannelGetRequester());
    PVStructure::shared_pointer pvRequest = CreateRequest::create()->createRequest(request);
    channel->createChannelGet(channelGetRequester, pvRequest);
    channelGetRequester->waitUntilDone(timeout);
}

void listProviders()
{
    cout << "Available channel providers:" << endl;
    std::auto_ptr<ChannelProviderRegistry::stringVector_t> providers =
            getChannelProviderRegistry()->getProviderNames();
    for (size_t i=0; i<providers->size(); ++i)
        cout << (i+1) << ") " << providers->at(i) << endl;
}

static void help(const char *name)
{
    cout << "USAGE: " << name << " [options] [channel]" << endl;
    cout << "  -h        : Help" << endl;
    cout << "  -r request: Request" << endl;
    cout << "  -w seconds: Wait timeout" << endl;
}

int main(int argc,char *argv[])
{
    string channel = "neutrons";
    string request = "field()";
    double timeout = 2.0;

    int opt;
    while ((opt = getopt(argc, argv, "r:w:h")) != -1)
    {
        switch (opt)
        {
        case 'r':
            request = optarg;
            break;
        case 'w':
            timeout = atof(optarg);
            break;
        case 'h':
            help(argv[0]);
            return 0;
        default:
            help(argv[0]);
            return -1;
        }
    }
    if (optind < argc)
        channel = argv[optind];

    cout << "Channel: " << channel << endl;
    cout << "Request: " << request << endl;
    cout << "Wait:    " << timeout << " sec" << endl;

    try
    {
        ClientFactory::start();
        listProviders();
        monitor(channel, request, timeout);
        epicsThreadSleep(5.0);
        ClientFactory::stop();
    }
    catch (std::exception &ex)
    {
        fprintf(stderr, "Exception: %s\n", ex.what());
        PRINT_EXCEPTION2(ex, stderr);
        cout << SHOW_EXCEPTION(ex);
    }
    return 0;
}