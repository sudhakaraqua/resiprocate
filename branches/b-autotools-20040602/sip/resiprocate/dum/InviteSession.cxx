#include "resiprocate/SipMessage.hxx"
#include "resiprocate/SdpContents.hxx"
#include "resiprocate/dum/Dialog.hxx"
#include "resiprocate/dum/DialogUsageManager.hxx"
#include "resiprocate/dum/InviteSession.hxx"
#include "resiprocate/dum/InviteSessionHandler.hxx"
#include "resiprocate/os/Logger.hxx"

#define RESIPROCATE_SUBSYSTEM Subsystem::DUM

using namespace resip;

InviteSession::InviteSession(DialogUsageManager& dum, Dialog& dialog)
   : BaseUsage(dum, dialog),
     mOfferState(Nothing),
     mCurrentLocalSdp(0),
     mCurrentRemoteSdp(0),
     mProposedLocalSdp(0),
     mProposedRemoteSdp(0)
{
   assert(mDum.mInviteSessionHandler);
}

InviteSession::~InviteSession()
{
   mDialog.mInviteSession = 0;
}

void
InviteSession::setOffer(const SdpContents* sdp)
{
   //mProposedLocalSdp = sdp;
}

void
InviteSession::setAnswer(const SdpContents* sdp)
{
   //mProposedLocalSdp = 0;
   //mCurrentLocalSdp = sdp;
}

const SdpContents* 
InviteSession::getLocalSdp()
{
   return mCurrentLocalSdp;
}

const SdpContents* 
InviteSession::getRemoteSdp()
{
   return mCurrentRemoteSdp;
}

void
InviteSession::dispatch(const SipMessage& msg)
{
   std::pair<OfferAnswerType, const SdpContents*> offans;
   offans = InviteSession::getOfferOrAnswer(msg);

   // reINVITE
   if (msg.isRequest())
   {
      switch(msg.header(h_RequestLine).method())
      {
         case INVITE:
            mDialog.update(msg);
            mDum.mInviteSessionHandler->onDialogModified(getSessionHandle(), msg);
                  
            if (offans.first != None)
            {
               InviteSession::incomingSdp(msg, offans.second);
            }
            break;

         case BYE:
            end();
            break;

         case UPDATE:
            assert(0);
            break;
                  
         case INFO:
            mDum.mInviteSessionHandler->onInfo(getSessionHandle(), msg);
            break;
                  
         case REFER:
            assert(0); // !jf! 
            mDum.mInviteSessionHandler->onRefer(getSessionHandle(), msg);
            break;
                  
         default:
            InfoLog (<< "Ignoring request in an INVITE dialog: " << msg.brief());
            break;
      }
   }
}


SipMessage&
InviteSession::end()
{
   //assert(mState == Connected);

#if 0
   // no way for the application to modify the BYE yet
   SipMessage bye;
   mDialog.makeBye(bye);
   copyAuthorizations(bye);
   //mDum.send(bye);
#endif
   return mLastRequest;
}

// If sdp==0, it means the last offer failed
void
InviteSession::incomingSdp(const SipMessage& msg, const SdpContents* sdp)
{
   switch (mOfferState)
   {
      case Nothing:
         assert(mCurrentLocalSdp == 0);
         assert(mCurrentRemoteSdp == 0);
         mProposedRemoteSdp = static_cast<SdpContents*>(sdp->clone());
         mOfferState = Offerred;
         mDum.mInviteSessionHandler->onOffer(getSessionHandle(), msg, sdp);
         break;
         
      case Offerred:
         mCurrentLocalSdp = mProposedLocalSdp;
         mCurrentRemoteSdp = static_cast<SdpContents*>(sdp->clone());
         mProposedLocalSdp = 0;
         mProposedRemoteSdp = 0;
         mOfferState = Answered;
         mDum.mInviteSessionHandler->onAnswer(getSessionHandle(), msg, sdp);
         break;

      case Answered:
         assert(mProposedLocalSdp == 0);
         assert(mProposedRemoteSdp == 0);
         mProposedRemoteSdp = static_cast<SdpContents*>(sdp->clone());
         mOfferState = CounterOfferred;
         mDum.mInviteSessionHandler->onOffer(getSessionHandle(), msg, sdp);
         break;
         
         
      case CounterOfferred:
         assert(mCurrentLocalSdp);
         assert(mCurrentRemoteSdp);
         mOfferState = Answered;
         if (sdp)
         {
            mCurrentLocalSdp = mProposedLocalSdp;
            mCurrentRemoteSdp = static_cast<SdpContents*>(sdp->clone());
            mDum.mInviteSessionHandler->onAnswer(getSessionHandle(), msg, sdp);
         }
         else
         {
            mProposedLocalSdp = 0;
            mProposedRemoteSdp = 0;
            // !jf! is this right? 
            mDum.mInviteSessionHandler->onOfferRejected(getSessionHandle(), msg);
         }
         break;
   }
}

void
InviteSession::sendSdp(const SdpContents* sdp)
{
   switch (mOfferState)
   {
      case Nothing:
         assert(mCurrentLocalSdp == 0);
         assert(mCurrentRemoteSdp == 0);
         mProposedLocalSdp = static_cast<SdpContents*>(sdp->clone());
         mOfferState = Offerred;
         break;
         
      case Offerred:
         mCurrentLocalSdp = static_cast<SdpContents*>(sdp->clone());
         mCurrentRemoteSdp = mProposedRemoteSdp;
         mProposedLocalSdp = 0;
         mProposedRemoteSdp = 0;
         mOfferState = Answered;
         break;

      case Answered:
         assert(mProposedLocalSdp == 0);
         assert(mProposedRemoteSdp == 0);
         mProposedLocalSdp = static_cast<SdpContents*>(sdp->clone());
         mOfferState = CounterOfferred;
         break;
        
         
      case CounterOfferred:
         assert(mCurrentLocalSdp);
         assert(mCurrentRemoteSdp);
         if (sdp)
         {
            mCurrentLocalSdp = static_cast<SdpContents*>(sdp->clone());
            mCurrentRemoteSdp = mProposedRemoteSdp;
         }
         else
         {
            mProposedLocalSdp = 0;
            mProposedRemoteSdp = 0;
         }
         mOfferState = Answered;
         break;
   }
}

std::pair<InviteSession::OfferAnswerType, const SdpContents*>
InviteSession::getOfferOrAnswer(const SipMessage& msg) const
{
   std::pair<InviteSession::OfferAnswerType, const SdpContents*> ret;
   ret.first = None;
   
   const SdpContents* contents = dynamic_cast<const SdpContents*>(msg.getContents());
   if (contents)
   {
      static Token c100rel(Symbols::C100rel);
      if (msg.isRequest() || 
          msg.exists(h_Supporteds) && msg.header(h_Supporteds).find(c100rel))
      {
         switch (mOfferState)
         {
            case None: 
               ret.first = Offer;
               ret.second = contents;
               break;
               
            case Offerred:
               ret.first = Answer;
               ret.second = contents;
               break;

            case Answered:
               ret.first = Offer;
               ret.second = contents;
               break;
               
            case CounterOfferred:
               ret.first = Answer;
               ret.second = contents;
               break;
         }
      }
   }
   return ret;
}

void
InviteSession::copyAuthorizations(SipMessage& request)
{
#if 0
   if (mLastRequest.exists(h_ProxyAuthorizations))
   {
      // should make the next auth (change nextNonce)
      request.header(h_ProxyAuthorizations) = mLastRequest.header(h_ProxyAuthorizations);
   }
   if (mLastRequest.exists(h_ProxyAuthorizations))
   {
      // should make the next auth (change nextNonce)
      request.header(h_ProxyAuthorizations) = mLastRequest.header(h_ProxyAuthorizations);
   }
#endif
}

InviteSession::Handle::Handle(DialogUsageManager& dum)
   : BaseUsage::Handle(dum)
{}

InviteSession*
InviteSession::Handle::operator->()
{
   return static_cast<InviteSession*>(get());
}



void InviteSession::makeAck(const SipMessage& response2xx)
{
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the

 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */