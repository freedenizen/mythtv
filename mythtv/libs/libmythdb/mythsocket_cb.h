/** -*- Mode: c++ -*- */
#ifndef _MYTHSOCKET_CB_H_
#define _MYTHSOCKET_CB_H_

#include "mythexp.h"

#define kMythSocketShortTimeout  7000
#define kMythSocketLongTimeout  30000

class MythSocket;
class MPUBLIC MythSocketCBs
{
  public:
    virtual ~MythSocketCBs() {}
    virtual void connected(MythSocket*) = 0;
    virtual void readyRead(MythSocket*) = 0;
    virtual void connectionFailed(MythSocket*) = 0;
    virtual void connectionClosed(MythSocket*) = 0;
};

#endif // _MYTHSOCKET_CB_H_
