#ifndef WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#endif

#include <stdio.h>
#include <errno.h>

#include "sip2/util/compat.hxx"
#include "sip2/util/Socket.hxx"
#include "sip2/util/Logger.hxx"

#include "sip2/sipstack/DnsResolver.hxx"
#include "sip2/sipstack/Symbols.hxx"
#include "sip2/sipstack/ParserCategories.hxx"
#include "sip2/sipstack/SipStack.hxx"
#include "sip2/sipstack/TransactionState.hxx"


#define VOCAL_SUBSYSTEM Vocal2::Subsystem::TRANSACTION

using namespace Vocal2;
using namespace std;

DnsResolver::DnsResolver(SipStack& stack) : mStack(stack)
{
#if defined(USE_ARES)
   int status=0;
   if ((status = ares_init(&mChannel)) != ARES_SUCCESS)
   {
      ErrLog (<< "Failed to initialize async dns library (ares)");
      char* errmem=0;
      ErrLog (<< ares_strerror(status, &errmem));
      ares_free_errmem(errmem);
      throw Exception("failed to initialize ares", __FILE__,__LINE__);
   }
#endif
}


DnsResolver::~DnsResolver()
{
#if defined(USE_ARES)
   ares_destroy(mChannel);
#endif
}


void
DnsResolver::buildFdSet(FdSet& fdset)
{
#if defined(USE_ARES)
   int size = ares_fds(mChannel, &fdset.read, &fdset.write);
   if ( size > fdset.size )
   {
      fdset.size = size;
   }
#endif
}

void
DnsResolver::process(FdSet& fdset)
{
#if defined(USE_ARES)
   ares_process(mChannel, &fdset.read, &fdset.write);
#endif
}

static int 
determinePort(const Data& scheme, Transport::Type transport)
{
   if ( isEqualNoCase(scheme, Symbols::Sips) || (transport == Transport::TLS) )
   {
      return Symbols::DefaultSipsPort;
   }
   
   return Symbols::DefaultSipPort;
}


#if defined(USE_ARES)
static Data
determineSrvPrefix(const Data& scheme, Transport::Type transport)
{
   Data prefix;

   if ( isEqualNoCase(scheme, Symbols::Sips) )
   {
      prefix = Symbols::SrvSips;
   }
   else
   {
      prefix = Symbols::SrvSip;
   }

   if ( (transport == Transport::TLS) || (transport == Transport::TCP) )
   {
      prefix += "." + Symbols::SrvTcp;
   }
   else
   {
      prefix += "." + Symbols::SrvUdp;
   }

   return prefix;
}
#endif

void
DnsResolver::lookup(const Data& transactionId, const Via& via)
{
   //duplicate entry has not been eliminated
   Transport::Type transport = Transport::toTransport(via.transport());
   Data& target = via.exists(p_maddr) ? via.param(p_maddr) : via.sentHost();
   if (via.exists(p_received))
   {
      if (via.exists(p_rport))
      {
         lookupARecords(transactionId, via.param(p_received), via.param(p_rport).port(), transport);
         // try with via.sentPort() too, even if via.exists(p_rport)?
      }
      else
      {
         if (via.sentPort())
         {
            lookupARecords(transactionId, via.param(p_received), via.sentPort(), transport);
            // try with default port too, even if via.sentPort()?
         }
         else
         {
            lookupARecords(transactionId, via.param(p_received),  determinePort(via.protocolName(), transport), transport);
         }
      }
   }
   else if (via.exists(p_rport))
   {
      lookupARecords(transactionId, target,  via.param(p_rport).port(), transport);
   }
   else if (via.sentPort())
   {
      lookupARecords(transactionId, target, via.sentPort(), transport);
      // try with default port too, even if via.sentPort()?
   }
   else
   {
      lookupARecords(transactionId, target, determinePort(via.protocolName(), transport), transport);
   }
}


void
DnsResolver::lookup(const Data& transactionId, const Uri& uri)
{
   Data& target = uri.exists(p_maddr) ? uri.param(p_maddr) : uri.host();
   bool isNumeric = isIpAddress(target);
   int port;
   
   Transport::Type transport;

   if (uri.scheme() == Symbols::Sips)
   {
      transport = Transport::TLS;
   }
   else
   {
      if (uri.exists(p_transport))
      {
         transport = Transport::toTransport(uri.param(p_transport));
      }
      else
      {
         if (isNumeric || uri.port() != 0)
         {
            if (uri.scheme() == Symbols::Sip)
            {
               transport = Transport::UDP;
            }
            else if (uri.scheme() == Symbols::Sips)
            {
               transport = Transport::TCP;
            }
            else
            {
               assert(0);
               mStack.mStateMacFifo.add(new DnsMessage(transactionId));
               return;
            }
         }
         else
         {
            DebugLog(<<"Should be doing NAPTR+SRV per RFC 3263 s4.1, 4.2");
            transport = Transport::UDP;

#if defined(USE_ARES)
            DebugLog(<<"For now doing TCP _and_ UDP SRV queries");

            Request* request = new Request(mStack, transactionId,
               target, 0, Transport::TCP, uri.scheme());

	    // Priority: SRV TCP, SRV UDP, A/AAAA
	    if (mStack.mTransportSelector.findTransport(Transport::TCP))
	    {
		request->otherTransports.push_back(Transport::TCP);
	    }
	    if (mStack.mTransportSelector.findTransport(Transport::UDP))
	    {
		request->otherTransports.push_back(Transport::UDP);
	    }
	    request->otherTransports.push_back(Transport::Unknown);

	    // Assume TCP and UDP as the only possibilities now.
	    assert(request->otherTransports.size() > 0);
	    if (request->otherTransports.front() == Transport::TCP)
	    {
		request->otherTransports.pop_front();
		Data srvTarget =
		   determineSrvPrefix(uri.scheme(), Transport::TCP)
		   + "." + target;
		ares_query(mChannel, srvTarget.c_str(), C_IN, T_SRV,
		   DnsResolver::aresCallbackSrvTcp, request);
	    }
	    else
	    {
		request->otherTransports.pop_front();
		Data srvTarget =
		   determineSrvPrefix(uri.scheme(), Transport::UDP)
		   + "." + target;
		ares_query(mChannel, srvTarget.c_str(), C_IN, T_SRV,
		   DnsResolver::aresCallbackSrvUdp, request);
	    }

            return;
#endif
         }
      }
   }

   if (uri.port())
   {
      port = uri.port();
   }
   else
   {
      port = determinePort(uri.scheme(), transport);
   }
         
   lookupARecords(transactionId, target, port, transport);         
}
 

void
DnsResolver::lookupARecords(const Data& transactionId, const Data& host, int port, Transport::Type transport)

{
   TransactionState* txn = mStack.mTransactionMap.find(transactionId);
   if (!txn)
   {
      DebugLog(<< "DNS lookup for non-existent transaction");
      return;
   }

#if defined(USE_ARES)
   Request* request = new Request(mStack, transactionId, host, port, transport, Data::Empty);
   ares_gethostbyname(mChannel, host.c_str(), AF_INET, DnsResolver::aresCallbackHost, request);
#else   
   struct hostent* result=0;
   int ret=0;
   int herrno=0;

#if defined(__linux__)
   struct hostent hostbuf; 
   char buffer[8192];
   ret = gethostbyname_r( host.c_str(), &hostbuf, buffer, sizeof(buffer), &result, &herrno);
   assert (ret != ERANGE);
#elif defined(WIN32) 
   result = gethostbyname( host.c_str() );
   herrno = WSAGetLastError();
#elif defined( __MACH__ ) || defined (__FreeBSD__)
   result = gethostbyname( host.c_str() );
   herrno = h_errno;
#elif defined(__QNX__) || defined(__SUNPRO_CC)
   struct hostent hostbuf; 
   char buffer[8192];
   result = gethostbyname_r( host.c_str(), &hostbuf, buffer, sizeof(buffer), &herrno );
#else
#   error "need to define some version of gethostbyname for your arch"
#endif

   if ( (ret!=0) || (result==0) )
   {
      switch (herrno)
      {
         case HOST_NOT_FOUND:
            InfoLog ( << "host not found: " << host);
            break;
         case NO_DATA:
            InfoLog ( << "no data found for: " << host);
            break;
         case NO_RECOVERY:
            InfoLog ( << "no recovery lookup up: " << host);
            break;
         case TRY_AGAIN:
            InfoLog ( << "try again: " << host);
            break;
		 default:
			 ErrLog( << "DNS Resolver got error" << herrno << " looking up " << host );
			 assert(0);
			 break;
      }
   }
   else
   {
      assert(result);
      assert(result->h_length == 4);
      
      DnsMessage* dns = new DnsMessage(transactionId);
      DebugLog (<< "DNS lookup of " << host << ": canonical name: " << result->h_name);
      for (char** pptr = result->h_addr_list; *pptr != 0; pptr++)
      {
         Transport::Tuple tuple;
         tuple.ipv4.s_addr = *((u_int32_t*)(*pptr));
         tuple.port = port;
         tuple.transportType = transport;
         tuple.transport = 0;
         
         DebugLog(<< tuple);
         dns->mTuples.push_back(tuple);
      }
      mStack.mStateMacFifo.add(dns);
   }
#endif
}

bool
DnsResolver::isIpAddress(const Data& data)
{
   // ok, this is fairly monstrous but it works. 
    // !cj! - won't work for IPV6
   unsigned int p1,p2,p3,p4;
   int count=0;
   int result = sscanf( data.c_str(), 
                        "%u.%u.%u.%u%n",
                        &p1, &p2, &p3, &p4, &count );

   if ( (result == 4) && (p1 <= 255) && (p2 <= 255) && (p3 <= 255) && (p4 <= 255) && (count == int(data.size())) )
   {
      return true;
   }
   else
   {
      return false;
   }
} 

#if defined(USE_ARES)
void 
DnsResolver::aresCallbackHost(void *arg, int status, struct hostent* result)
{
   std::auto_ptr<Request> request(reinterpret_cast<Request*>(arg));

   DebugLog (<< "Received dns update: " << request->tid);
   DnsMessage* dns = new DnsMessage(request->tid);

   if (status != ARES_SUCCESS)
   {
      char* errmem=0;
      InfoLog (<< "Failed async dns query: " << ares_strerror(status, &errmem));
      ares_free_errmem(errmem);
   }
   else
   {
      DebugLog (<< "DNS lookup canonical name: " << result->h_name);
      for (char** pptr = result->h_addr_list; *pptr != 0; pptr++)
      {
	 Transport::Tuple tuple;
	 tuple.ipv4.s_addr = *((u_int32_t*)(*pptr));
	 tuple.port = request->port;
	 tuple.transportType = request->transport;
	 tuple.transport = 0;
	 
	 DebugLog(<< tuple);
	 dns->mTuples.push_back(tuple);
      }
   }
   if (request->isFinal)
   {
      dns->isFinal = true;
   }
   request->stack.mStateMacFifo.add(dns);
}
#endif

#if defined(USE_ARES)
void
DnsResolver::aresCallbackNaptr(void *arg, int pstatus,
   unsigned char *abuf, int alen)
{
   int status, len;
   Request *request = reinterpret_cast<Request*>(arg);
   char *name, *errmem;
   unsigned char *rrindex;
   unsigned char *aptr = abuf + HFIXEDSZ;
   DnsResolver::NaptrSet *naptrset = new DnsResolver::NaptrSet;

   DebugLog (<< "Received NAPTR result for: " << request->tid << " for "
             << request->host);

   if (pstatus != ARES_SUCCESS)
   {
      errmem = 0;
      InfoLog (<< "NAPTR lookup failed: " << ares_strerror(pstatus, &errmem));
      ares_free_errmem(errmem);
      assert(0); // Do the right thing!
   }

   for (int i = 0; i < DNS_HEADER_ANCOUNT(abuf); i++)
   {
      DnsResolver::Naptr *naptr = new DnsResolver::Naptr;

      status = ares_expand_name(aptr, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS RR: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto NaptrParseDone;
      }

      aptr += len;
      // int dlen = DNS_RR_LEN(aptr);
      aptr += RRFIXEDSZ;

      naptr->order = DNS__16BIT(aptr);
      naptr->pref = DNS__16BIT(aptr+2);

      rrindex = aptr+4;
      status = ares_expand_name(rrindex, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS answer: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto NaptrParseDone;
      }
      naptr->flags = name;

      rrindex += len + 1;
      status = ares_expand_name(rrindex, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS answer: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto NaptrParseDone;
      }
      naptr->service = name;

      rrindex += len + 1;
      status = ares_expand_name(rrindex, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS answer: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto NaptrParseDone;
      }
      naptr->regex = name;

      rrindex += len + 1;
      status = ares_expand_name(rrindex, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS answer: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto NaptrParseDone;
      }
      naptr->replacement = name;

      naptrset->insert(*naptr);
   }

NaptrParseDone:
   for (DnsResolver::NaptrIterator n = naptrset->begin();
	n != naptrset->end();
	n++)
   {
      DebugLog(<< "NAPTR entry [" << n->order << ", " << n->pref << ", " 
	       << n->flags << ", " << n->service << ", " << n->regex
	       << ", " << n->replacement);
   }
   delete request;
}
#endif

#if defined(USE_ARES)
static DnsResolver::SrvSet&
aresParseSrv(int pstatus, unsigned char *abuf, int alen,
    Transport::Type transport)
{
   int status, len;
   char *name, *errmem;
   const unsigned char *aptr = abuf + HFIXEDSZ;
   DnsResolver::SrvSet *srvset = new DnsResolver::SrvSet;

   if (pstatus != ARES_SUCCESS)
   {
      errmem = 0;
      InfoLog (<< "SRV lookup failed: " << ares_strerror(pstatus, &errmem));
      ares_free_errmem(errmem);
      return *srvset;
   }

   // !rk!  Throw away all of the "questions" in the DNS packet.  Is there
   // not just a way to skip right over these without iterating like this?
   for (int i = 0; i < DNS_HEADER_QDCOUNT(abuf); i++)
   {
      status = ares_expand_name(aptr, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS question: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto SrvParseDone;
      }
      free(name);
      aptr += len + QFIXEDSZ;
   }

   for (int i = 0; i < DNS_HEADER_ANCOUNT(abuf); i++)
   {
      DnsResolver::Srv *srv = new DnsResolver::Srv;

      status = ares_expand_name(aptr, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS RR: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto SrvParseDone;
      }

      aptr += len;
      int dlen = DNS_RR_LEN(aptr);
      aptr += RRFIXEDSZ;

      srv->priority = DNS__16BIT(aptr);
      srv->weight = DNS__16BIT(aptr+2);
      srv->port = DNS__16BIT(aptr+4);

      status = ares_expand_name(aptr+6, abuf, alen, &name, &len);
      if (status != ARES_SUCCESS)
      {
         errmem = 0;
         InfoLog (<< "Bad DNS answer: " << ares_strerror(status, &errmem));
         ares_free_errmem(errmem);
         goto SrvParseDone;
      }
      srv->host = name;
      srv->transport = transport;
      srvset->insert(*srv);

      free(name);
      aptr += dlen;
   }

SrvParseDone:
   for (DnsResolver::SrvIterator s = srvset->begin(); s != srvset->end(); s++)
   {
      DebugLog(<< "SRV entry " << s->host << " with priority " << s->priority);
   }

   return *srvset;
}
#endif

#if defined(USE_ARES)
void
DnsResolver::aresCallbackSrvTcp(void *arg, int pstatus,
   unsigned char *abuf, int alen)
{
   Request *request = reinterpret_cast<Request*>(arg);

   DebugLog (<< "Received SRV/TCP result for: " << request->tid << " for "
             << request->host);

   if (pstatus == ARES_EDESTRUCTION)
   {
      return;
   }

   SrvSet& srvset = aresParseSrv(pstatus, abuf, alen, Transport::TCP);

   if (request->otherTransports.size())
   {
      Transport::Type next = request->otherTransports.front();
      request->otherTransports.pop_front();
      if (next == Transport::UDP)
      {
	 Data srvTarget =
	    determineSrvPrefix(request->scheme, Transport::UDP)
	    + "." + request->host;
	 ares_query(request->stack.mDnsResolver.mChannel, srvTarget.c_str(),
	    C_IN, T_SRV,
	    DnsResolver::aresCallbackSrvUdp, new Request(*request));
      }
      else if (next == Transport::Unknown)
      {
	 // Add the request target as the only result
	 InfoLog (<< "Adding fallback SRV to queue A/AAAA lookup");
	 Srv srv;
	 srv.priority = 65535;
	 srv.weight = 0;
	 srv.port = determinePort(request->scheme, Transport::Unknown);
	 srv.host = request->host;
	 srv.transport = Transport::TCP;
	 srvset.insert(srv);
      }
      else
      {
	 assert(0);
      }
   }

   for (DnsResolver::SrvIterator s = srvset.begin(); s != srvset.end(); s++)
   {
       Request* resolve = new Request(request->stack,
				      request->tid,
				      s->host,
				      s->port,
				      s->transport,
				      Data::Empty);
       if (!request->otherTransports.size())
       {
           resolve->isFinal = true;
       }
       ares_gethostbyname(request->stack.mDnsResolver.mChannel, resolve->host.c_str(), AF_INET, DnsResolver::aresCallbackHost, resolve);
   }

   delete request;
}
#endif

#if defined(USE_ARES)
void
DnsResolver::aresCallbackSrvUdp(void *arg, int pstatus,
   unsigned char *abuf, int alen)
{
   Request *request = reinterpret_cast<Request*>(arg);

   DebugLog (<< "Received SRV/UDP result for: " << request->tid << " for "
             << request->host);
   
   if (pstatus == ARES_EDESTRUCTION)
   {
      return;
   }

   SrvSet& srvset = aresParseSrv(pstatus, abuf, alen, Transport::UDP);

   if (request->otherTransports.size())
   {
      Transport::Type next = request->otherTransports.front();
      request->otherTransports.pop_front();
      if (next == Transport::TCP)
      {
	 Data srvTarget =
	    determineSrvPrefix(request->scheme, Transport::TCP)
	    + "." + request->host;
	 ares_query(request->stack.mDnsResolver.mChannel, srvTarget.c_str(),
	    C_IN, T_SRV,
	    DnsResolver::aresCallbackSrvTcp, new Request(*request));
      }
      else if (next == Transport::Unknown)
      {
	 // Add the request target as the only result
	 InfoLog (<< "Adding fallback SRV to queue A/AAAA lookup");
	 Srv srv;
	 srv.priority = 65535;
	 srv.weight = 0;
	 srv.port = determinePort(request->scheme, Transport::Unknown);
	 srv.host = request->host;
	 srv.transport = Transport::UDP;
	 srvset.insert(srv);
      }
      else
      {
	 assert(0);
      }
   }

   for (DnsResolver::SrvIterator s = srvset.begin(); s != srvset.end(); s++)
   {
       Request* resolve = new Request(request->stack,
				      request->tid,
				      s->host,
				      s->port,
				      s->transport,
				      Data::Empty);
       if (!request->otherTransports.size())
       {
           resolve->isFinal = true;
       }
       ares_gethostbyname(request->stack.mDnsResolver.mChannel, resolve->host.c_str(), AF_INET, DnsResolver::aresCallbackHost, resolve);

   }

   delete request;
}
#endif

Data 
DnsResolver::DnsMessage::brief() const 
{ 
   Data result;
   DataStream strm(result);
   strm << "DnsMessage: tid=" << mTransactionId;
   // could output the tuples
   strm.flush();
   return result;
}

std::ostream& 
DnsResolver::DnsMessage::encode(std::ostream& strm) const
{
   strm << "Dns: tid=" << mTransactionId;
   for (DnsResolver::TupleIterator i=mTuples.begin(); i != mTuples.end(); i++)
   {
      strm << *i << ",";
   }
   return strm;
}
