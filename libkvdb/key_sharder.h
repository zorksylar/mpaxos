#ifndef __KEY_SHARDER_H__
#define __KEY_SHARDER_H__


#include <cstdio>
#include "lock.h"



template <typename Key>
class KeySharder {
public :
    
    typedef Key key_type;

    KeySharder(){};
    
    KeySharder(key_type)

}


