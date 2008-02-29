#if !defined(resip_Vthread_hxx)
#define resip_Vthread_hxx


#ifdef WIN32

#error this should not be used in win32 

#endif


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


static const char* const resip_vthread_h_Version =
    "$Id: vthread.hxx,v 1.4 2003/03/25 06:33:48 jason Exp $";


#include <pthread.h>


typedef pthread_t vthread_t;
typedef pthread_mutex_t vmutex_t;
typedef pthread_cond_t vcondition_t;
typedef pthread_attr_t vthread_attr_t;    


#define     vmutex_init(mutex) \
                pthread_mutex_init((mutex),0)
                

#define     vmutex_destroy(mutex) \
                pthread_mutex_destroy((mutex))
                

#define     vmutex_lock(mutex) \
                pthread_mutex_lock((mutex))


#define     vmutex_unlock(mutex) \
                pthread_mutex_unlock((mutex))
                

#define     vcond_init(cond) \
                pthread_cond_init((cond),0)
                

#define     vcond_destroy(cond) \
                pthread_cond_destroy((cond))
                

#define     vcond_wait(cond, mutex) \
                pthread_cond_wait((cond),(mutex))


#define     vcond_timedwait(cond, mutex, timeout) \
                pthread_cond_timedwait((cond),(mutex),(timeout))


#define     vcond_signal(cond) \
                pthread_cond_signal((cond))


#define     vcond_broadcast(cond) \
                pthread_cond_broadcast((cond))
                



#endif // !defined(VOCAL_POSIX_VTHREAD_H)