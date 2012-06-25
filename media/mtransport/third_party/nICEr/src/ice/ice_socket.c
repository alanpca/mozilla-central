/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



static char *RCSSTRING __UNUSED__="$Id: ice_socket.c,v 1.2 2008/04/28 17:59:01 ekr Exp $";

#include <assert.h>
#include "nr_api.h"
#include "ice_ctx.h"
#include "stun.h"

    
static void nr_ice_socket_readable_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    int r;
    nr_ice_stun_ctx *sc1,*sc2;
    nr_ice_socket *sock=cb_arg;
    UCHAR buf[8192];
    char string[256];
    nr_transport_addr addr;
    int len;
    int is_stun;
    int is_req;
    int is_ind;
    nr_socket *stun_srv_sock=sock->sock;

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Socket ready to read",sock->ctx->label);
    
    /* Re-arm first! */
    NR_ASYNC_WAIT(s,how,nr_ice_socket_readable_cb,cb_arg);

    if(r=nr_socket_recvfrom(sock->sock,buf,sizeof(buf),(size_t *)&len,0,&addr)){
      r_log(LOG_ICE,LOG_ERR,"ICE(%s): Error reading from socket",sock->ctx->label);
      return;
    }

#ifdef USE_TURN
  re_process:
#endif /* USE_TURN */
    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Read %d bytes",sock->ctx->label,len);

    /* First question: is this STUN or not? */
    is_stun=nr_is_stun_message(buf,len);

    if(is_stun){
      snprintf(string, sizeof(string)-1, "ICE(%s): Message is STUN",sock->ctx->label);
      r_dump(NR_LOG_STUN, LOG_DEBUG, string, (char*)buf, len);

      is_req=nr_is_stun_request_message(buf,len);
      is_ind=is_req?0:nr_is_stun_indication_message(buf,len);

      /* We need to offer it to all of our stun contexts
         to see who bites */
      sc1=TAILQ_FIRST(&sock->stun_ctxs);
      while(sc1){
        sc2=TAILQ_NEXT(sc1,entry);

        r=-1;
        switch(sc1->type){
          /* This has been deleted, prune... */
          case NR_ICE_STUN_NONE:
            TAILQ_REMOVE(&sock->stun_ctxs,sc1,entry);
            RFREE(sc1);
            break;

          case NR_ICE_STUN_CLIENT:
            if(!(is_req||is_ind)){
                r=nr_stun_client_process_response(sc1->u.client,buf,len,&addr);
            }
            break;

          case NR_ICE_STUN_SERVER:
            if(is_req){
              r=nr_stun_server_process_request(sc1->u.server,stun_srv_sock,(char *)buf,len,&addr,NR_STUN_AUTH_RULE_SHORT_TERM);
            }
            break;

#ifdef USE_TURN
          case NR_ICE_TURN_CLIENT:
            /* data indications are ok, so don't ignore those */
            if(!is_req){
              if(!is_ind)
                r=nr_turn_client_process_response(sc1->u.turn_client,buf,len,&addr);
              else{
                /* This is a bit of a hack. If it's a data indication, strip
                   off the TURN framing and re-enter. We don't care about
                   other kinds of indication */
                nr_transport_addr n_addr;
                size_t n_len;
                
                r=nr_turn_client_rewrite_indication_data(buf,len,&n_len,&n_addr);
                if(!r){
                  r_log(LOG_ICE,LOG_DEBUG,"Unwrapped a data indication.");
                  len=n_len;
                  nr_transport_addr_copy(&addr,&n_addr);
                  stun_srv_sock=sc1->u.turn_client->wrapping_sock;
                  goto re_process;
                }
              }
            }
            break;
#endif /* USE_TURN */

          default:
            assert(0); /* Can't happen */
            return;
        }
        if(!r){
          break;
        }        

        sc1=sc2;
      }
      if(!sc1){
        if (nr_ice_ctx_is_known_id(sock->ctx,((nr_stun_message_header*)buf)->id.octet))
            r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Message is a retransmit",sock->ctx->label);
        else
            r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Message does not correspond to any registered stun ctx",sock->ctx->label);
      }
    }
    else{
      r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Message is not STUN",sock->ctx->label);
      
      nr_ice_ctx_deliver_packet(sock->ctx, sock->component, &addr, buf, len);
    }

    return;
  }

int nr_ice_socket_create(nr_ice_ctx *ctx,nr_ice_component *comp, nr_socket *nsock, nr_ice_socket **sockp)
  {
    nr_ice_socket *sock=0;
    NR_SOCKET fd;
    int _status;

    if(!(sock=RCALLOC(sizeof(nr_ice_socket))))
      ABORT(R_NO_MEMORY);

    sock->sock=nsock;
    sock->ctx=ctx;
    sock->component=comp;

    TAILQ_INIT(&sock->candidates);
    TAILQ_INIT(&sock->stun_ctxs);
    
    nr_socket_getfd(nsock,&fd);
    NR_ASYNC_WAIT(fd,NR_ASYNC_WAIT_READ,nr_ice_socket_readable_cb,sock);

    *sockp=sock;

    _status=0;
  abort:
    if(_status) RFREE(sock);
    return(_status);
  }


int nr_ice_socket_destroy(nr_ice_socket **isockp)
  {
    nr_ice_stun_ctx *s1,*s2;
    nr_ice_socket *isock;
    
    if(!isockp || !*isockp)
      return(0);
    
    isock=*isockp;
    *isockp=0;

    /* Close the socket */
    nr_ice_socket_close(isock);
    
    /* The STUN server */
    nr_stun_server_ctx_destroy(&isock->stun_server);

    /* Now clean up the STUN ctxs */
    TAILQ_FOREACH_SAFE(s1, &isock->stun_ctxs, entry, s2){
      TAILQ_REMOVE(&isock->stun_ctxs, s1, entry);
      RFREE(s1);
    }

    RFREE(isock);
    
    return(0);
  }

int nr_ice_socket_close(nr_ice_socket *isock)
  {
    NR_SOCKET fd=-1;

    if (!isock||!isock->sock)
      return(0);

    nr_socket_getfd(isock->sock,&fd);
    assert(isock->sock!=0);
    if(fd!=-1){
      NR_ASYNC_CANCEL(fd,NR_ASYNC_WAIT_READ);
      NR_ASYNC_CANCEL(fd,NR_ASYNC_WAIT_WRITE);
      nr_socket_destroy(&isock->sock);
    }
    
    return(0);
  }

int nr_ice_socket_register_stun_client(nr_ice_socket *sock, nr_stun_client_ctx *srv,void **handle)
  {
    nr_ice_stun_ctx *sc=0;
    int _status;

    if(!(sc=RCALLOC(sizeof(nr_ice_stun_ctx))))
      ABORT(R_NO_MEMORY);

    sc->type=NR_ICE_STUN_CLIENT;
    sc->u.client=srv;

    TAILQ_INSERT_TAIL(&sock->stun_ctxs,sc,entry);
    
    *handle=sc;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_socket_register_stun_server(nr_ice_socket *sock, nr_stun_server_ctx *srv,void **handle)
  {
    nr_ice_stun_ctx *sc=0;
    int _status;

    if(!(sc=RCALLOC(sizeof(nr_ice_stun_ctx))))
      ABORT(R_NO_MEMORY);

    sc->type=NR_ICE_STUN_SERVER;
    sc->u.server=srv;

    TAILQ_INSERT_TAIL(&sock->stun_ctxs,sc,entry);

    *handle=sc;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_socket_register_turn_client(nr_ice_socket *sock, nr_turn_client_ctx *srv,void **handle)
  {
    nr_ice_stun_ctx *sc=0;
    int _status;

    if(!(sc=RCALLOC(sizeof(nr_ice_stun_ctx))))
      ABORT(R_NO_MEMORY);

    sc->type=NR_ICE_TURN_CLIENT;
    sc->u.turn_client=srv;

    TAILQ_INSERT_TAIL(&sock->stun_ctxs,sc,entry);

    *handle=sc;

    _status=0;
  abort:
    return(_status);
  }

/* Just mark it deregistered. Don't delete it now because it's not safe
   in the CB, which is where this is likely to be called */
int nr_ice_socket_deregister(nr_ice_socket *sock, void *handle)
  {
    nr_ice_stun_ctx *sc=handle;

    if(!sc)
      return(0);
    
    sc->type=NR_ICE_STUN_NONE;

    return(0);
  }

