/**





spread.connect( daemon, name, 
          connect_callback(mb),
           );

spread.join( mb, group );
spread.leave( mb, group );
spread.multicast( mb, msgType, group, jsonData ); 
spread.disconnect( mb );

spread.receive( mb, callback(jsonTextRecv) );



jsonTextRecv = {
   event: JOIN|LEAVE|DISCONNECT|NETWORK_FAULT|DATA,
   group: ...,   // group associated with this message
   sender: ...,  // sender or cause of message 
   data: {
     ...         // user specified data, value of jsonData in sp.multicast
   }
}



var spread = require("...");

function on_connect( mb, error ) {
    if ( error.length > 0 )
    {
       // connection failed error contains the description 
       // why.  
    }    
    else
    {
       spread.join( mb, "some message group" );
    

       // save mb for later use when we want to send a message
       // like this.
       spread.multicast( mb, SAFE_MESS, "groupname", data );
    }
}

function on_receive( mb, json )
{

    ...

    spread.receive( mb, on_receive );
}


function on_message( mb, jsonText ) {
}

spread.connect( "4803@tsX", "sm", on_connect, on_message );
    



------------------------- In this module --------------------------------

Async -> attempt blocking call on connect otherwise use
         poll to determine if there is socket I/O on the spread
         connection and enqueue the message for later processing

AfterAsync -> 
     Is there an an error? 
         { event:ERROR, errorMsg: ... }
     Is this a membership message?
         { event: [JOIN|LEAVE|DISCONNECT|NETWORK], members in group, sender }
     Is this a regular message
         { event: DATA, data: jsonData from multicast of sender }  

*/
//#define BUILDING_NODE_EXTENSION

#include <string.h>
#include <stdlib.h>
#include <node.h> // v8 engine
#include "sp.h"   // spread toolkit

//#define DEBUG 0



using namespace v8;
 

// libuv allows us to pass around a pointer to an arbitrary
// object when running asynchronous functions. We create a
// data structure to hold the data we need during and after
// the async work.
typedef struct _AsyncData {
    int oper; 
    int mbox;
    char daemon[MAX_PROC_NAME];
    char name[MAX_PRIVATE_NAME];
    char errorMsg[1024];
    char eventType[32]; 
    char group[MAX_GROUP_NAME];    
    char sender[MAX_GROUP_NAME];
    char* data;
    int datalen; 
    service msgType;
    int membership_msgs;
    
    int has_callback;
    Persistent<Function> callback; // callback function
} AsyncData ;




// detect an error, if detected return -1 and set the
// msgType == error and set the errorMsg to provide
// a description of the error.
static int error_check(AsyncData* asyncData, int errcode )
{
    asyncData->errorMsg[0] = '\0';
    switch( errcode )
    {
 	case ILLEGAL_SESSION:
            strcpy(asyncData->errorMsg, 
                "The mbox given to receive on was illegal.");
            break;

        case ILLEGAL_MESSAGE:
            strcpy(asyncData->errorMsg,
                "The message had an illegal structure");
            break;

        case CONNECTION_CLOSED:
            strcpy(asyncData->errorMsg,
                 "Spread TCP connection is closed");
            break;

        
  	case GROUPS_TOO_SHORT:
            strcpy(asyncData->errorMsg,
                "GROUPS_TOO_SHORT: This addon does not support multigroup multicast");
            break;  
            
  	case ILLEGAL_SPREAD:
            strcpy(
               asyncData->errorMsg,
               "ILLEGAL_SPREAD, this is a generic error message check the spread.log"
            );  
            break;

  	case COULD_NOT_CONNECT:
            sprintf(
                asyncData->errorMsg,
                "Could not connect to %s",
                asyncData->daemon 
            );
            break;
 
                          
        case REJECT_VERSION:
            strcpy(asyncData->errorMsg,
                "daemon library version mismatch"
            );
            break;

        case REJECT_NO_NAME:
            strcpy(asyncData->errorMsg,
                "zero length name was given"
            );
            break;

        case REJECT_ILLEGAL_NAME: 	
            strcpy(asyncData->errorMsg, 
              "Name provided violated some requirement (length or used an illegal character)"
            );
            break;
 
  	case REJECT_NOT_UNIQUE:
            sprintf(asyncData->errorMsg, 
              "spread name=%s is not unique",
              asyncData->name 
            );        
            break; 
    }

    if ( asyncData->errorMsg[0] != '\0' )
    {
        strcpy(asyncData->eventType,"ERROR"); 
        return -1;
    }
    return 0;
}  



/////////////////// Multicast //////////////////////

void Multicast_AsyncWork(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;
    int n = SP_multicast( 
        asyncData->mbox, 
        asyncData->msgType,
        asyncData->group,
        0,
        asyncData->datalen,
        asyncData->data   
    );

    free( asyncData->data );
    if ( n < 1 ) 
    {       
        error_check( asyncData, n );
    }

#ifdef DEBUG
printf("SP_multicast called -> send %d bytes to %s\n", n, asyncData->group);
#endif
}


void Multicast_AsyncAfter(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;

    if ( asyncData->errorMsg[0] != '\0' && asyncData->has_callback )
    {
        Handle<Value> argv[] = {
            Number::New(asyncData->mbox),
            String::New(asyncData->errorMsg)
        };

        // surround in a try/catch for safety
        TryCatch try_catch;

        // execute the callback function
        asyncData->callback->Call(Context::GetCurrent()->Global(), 2, argv);
        if (try_catch.HasCaught())
            node::FatalException(try_catch);
        // dispose the Persistent handle so the callback
        // function can be garbage-collected
        asyncData->callback.Dispose();

    } 


    delete asyncData;
    delete req;
}



Handle<Value> spread_multicast(const Arguments& args) {

    HandleScope scope;

    // create an async work token
    uv_work_t *req = new uv_work_t;
    

    // assign our data structure that will be passed around
    AsyncData *asyncData = new AsyncData;
    req->data = asyncData;
    
    memset(asyncData,0,sizeof(AsyncData));


    asyncData->mbox = args[0]->Uint32Value();
    asyncData->msgType = args[1]->Uint32Value();
    strncpy(asyncData->group,
       *String::Utf8Value(args[2]->ToString()),
       sizeof(asyncData->group)-1);

    v8::String::Utf8Value data(args[3]->ToString());
    asyncData->datalen = strlen( *data );
    asyncData->data = strdup( *data );     

    if ( args.Length() == 5 )
    {
        asyncData->callback = Persistent<Function>::New(
        Local<Function>::Cast(args[4]));
        asyncData->has_callback = 1;
    }

   
    // pass the work token to libuv to be run when a
    // worker-thread is available to
    uv_queue_work(
       uv_default_loop(),
       req, // work token
       Multicast_AsyncWork, // work function
       (uv_after_work_cb) Multicast_AsyncAfter // function to run when complete
    );
    return scope.Close(Undefined());

}
 




/////////////////// Receive ////////////////////////



void Receive_AsyncWork(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;
    service service_type;
    char groups[32][MAX_GROUP_NAME];
    int num_groups=1;
    int16 mess_type;
    int endian_mismatch;
    int max_mess_len = 100000;
    int error = 0;
    int exit_loop = 0;
    int bytes;

    asyncData->data = (char *) calloc( 1, max_mess_len );
    
#ifdef DEBUG
printf("Entering Receive_AsyncWork\n"); 
#endif

    memset( groups, 0, sizeof(groups) );

    do
    {
        bytes = SP_receive(asyncData->mbox, &service_type, asyncData->sender, 
                   32, &num_groups, groups, 
                   &mess_type, &endian_mismatch, max_mess_len, asyncData->data);


#ifdef DEBUG
printf("Receive_AsyncWork got %d bytes\n", bytes); 
#endif


        if ( bytes < 0 )
        {
           error = bytes; 
        }

        if ( error == BUFFER_TOO_SHORT )
        {
            /*
               if BUFFER_TOO_SHORT error endian_mismatch is set to negative of 
               incoming message size. Yes SP_receive is a terrible API.
            */
#ifdef DEBUG
printf("Receive_AsyncWork buffer too short\n"); 
#endif


            max_mess_len = endian_mismatch;
            free( asyncData->data );
            asyncData->data = (char *) calloc( 1, max_mess_len ); 
        }
        //else if ( error == GROUPS_TOO_SHORT )
        //{
        //} 
        else if ( (error == 0) && Is_transition_mess( service_type ) )
        {
#ifdef DEBUG
printf("Receive_AsyncWork transition message\n"); 
#endif
            // discard this message and stay in loop
        } 
        else if ( (error == 0) && Is_reject_mess( service_type ) )
        {
#ifdef DEBUG
printf("Receive_AsyncWork reject message\n"); 
#endif
            // discard this message and stay in loop
        } 
        else
        {
            // the message is either an error we can't handle, or
            // a regular message, or a membership message
#ifdef DEBUG
printf("Receive_AsyncWork exit loop\n"); 
#endif

            asyncData->datalen = bytes;
            exit_loop = 1; 
        }

    }while(exit_loop == 0); 



    if ( error_check( asyncData, error ) != -1 )
    {
        strcpy( asyncData->group, groups[0] ); 


        // proceed with message, determine is its a membership message
        if      (Is_regular_mess( service_type ))
        {
            strcpy(asyncData->eventType,"DATA");
             
        } 
        else if (Is_caused_join_mess( service_type ))
        {
            strcpy(asyncData->eventType,"JOIN"); 
        } 
        else if (Is_caused_leave_mess( service_type ))
        {
            strcpy(asyncData->eventType,"LEAVE"); 
        } 
        else if (Is_caused_disconnect_mess( service_type ))
        {
            strcpy(asyncData->eventType,"DISCONNECT"); 
        } 
        else if (Is_caused_network_mess( service_type ))
        {
            strcpy(asyncData->eventType,"NETWORK"); 
        } 

        if (Is_membership_mess(service_type))
        {
            membership_info memb_info;
  
            SP_get_memb_info( asyncData->data, service_type, &memb_info );

            strcpy(asyncData->group, asyncData->sender ); 
            strcpy(asyncData->sender, memb_info.changed_member ); 
        }    

    }// valid data or membership message
    else 
    {
        switch ( error )  
        {
            // check for fatal connection errors
            case CONNECTION_CLOSED:
            case ILLEGAL_SESSION: 
                // our mailbox is no longer valid
                SP_disconnect( asyncData->mbox );
                asyncData->mbox = -1;
                break;  
        }
    }

    // finished 

#ifdef DEBUG
printf("Receive_AsyncWork existed %s\n", asyncData->errorMsg); 
#endif

}




// Function to execute when the async work is complete
// this function will be run inside the main event loop
// so it is safe to use V8 again
void Receive_AsyncAfter(uv_work_t *req) {
    HandleScope scope;
    // fetch our data structure
    AsyncData *asyncData = (AsyncData *)req->data;


#ifdef DEBUG
printf("entering Receive_AsyncAfter\n");
#endif

    char* json;
    char  small_message[32767];
    char  nullstr[] = { '\0' };
    char* data=nullstr;
    int   datalen=sizeof(nullstr);
    
    if ( strcmp(asyncData->eventType,"ERROR") == 0 )
    {
        json = small_message;

        sprintf(json, 
                "{"
                   "\"eventType\":\"ERROR\","
                   "\"errorMsg\" :\"%s\""   
                "}\n", asyncData->errorMsg );
    }
    else if ( strcmp(asyncData->eventType,"DATA") == 0 )
    {
        json = (char*) calloc(1,asyncData->datalen+4096);

        sprintf(json,
               "{"
                   "\"eventType\":\"DATA\","
               //    "\"data\"     : %*s,"
                   "\"group\"    :\"%s\","
                   "\"sender\"   :\"%s\""  
               "}\n",
                //asyncData->datalen, asyncData->data,
                asyncData->group,    
                asyncData->sender
        ); 

        data = asyncData->data;
        datalen = asyncData->datalen;  
    }
    else
    {
        json = small_message;

        sprintf(json,
              "{"
                  "\"eventType\":\"%s\","  
                  "\"group\"    :\"%s\","
                  "\"sender\"   :\"%s\""  
              "}\n",
               asyncData->eventType,      
               asyncData->group,
               asyncData->sender
        ); 
    }
 
    Handle<Value> argv[] = {
       Number::New(asyncData->mbox),
       String::New(json),
       String::New(data,datalen)
    };



    // surround in a try/catch for safety
    TryCatch try_catch;

    // execute the callback function
    asyncData->callback->Call(Context::GetCurrent()->Global(), 3, argv);
    if (try_catch.HasCaught())
      node::FatalException(try_catch);
    // dispose the Persistent handle so the callback
    // function can be garbage-collected
    asyncData->callback.Dispose();

    if ( asyncData->data )
    {
        free(  asyncData->data );
    }

    if ( json != small_message )
    {
       free( json );  
    }
    
    // clean up any memory we allocated
    delete asyncData;
    delete req;

}






Handle<Value> spread_receive(const Arguments& args) {

    HandleScope scope;

    // create an async work token
    uv_work_t *req = new uv_work_t;

    // assign our data structure that will be passed around
    AsyncData *asyncData = new AsyncData;
    req->data = asyncData;
    
    memset(asyncData,0,sizeof(AsyncData));


    asyncData->mbox = args[0]->Uint32Value();
    asyncData->callback = Persistent<Function>::New(
    Local<Function>::Cast(args[1]));
   
    // pass the work token to libuv to be run when a
    // worker-thread is available to
    uv_queue_work(
       uv_default_loop(),
       req, // work token
       Receive_AsyncWork, // work function
       (uv_after_work_cb) Receive_AsyncAfter // function to run when complete
    );
    return scope.Close(Undefined());

}
 


/////////////////// Disconnect //////////////////////

Handle<Value> spread_disconnect(const Arguments& args) {


    HandleScope scope;

#ifdef DEBUG
printf("entering spread_leave\n");
#endif


    int mbox = args[0]->Uint32Value();

    int err = SP_disconnect( mbox );

    return scope.Close(Number::New(err));
}


////////////////// Leave ///////////////////////////

void Leave_AsyncWork(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;


#ifdef DEBUG
    int error = SP_leave( asyncData->mbox, asyncData->group );
printf("SP_leave called -> %d\n", error);
#else
    SP_leave( asyncData->mbox, asyncData->group );
#endif
}


void Leave_AsyncAfter(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;

    delete asyncData;
    delete req;
}


Handle<Value> spread_leave(const Arguments& args) {
    
    HandleScope scope;

#ifdef DEBUG
printf("entering spread_leave\n");
#endif

    // create an async work token
    uv_work_t *req = new uv_work_t;

    // assign our data structure that will be passed around
    AsyncData *asyncData = new AsyncData;
    req->data = asyncData;
    
    memset(asyncData,0,sizeof(AsyncData));

    asyncData->mbox = args[0]->Uint32Value();
    strncpy(asyncData->group,
       *String::Utf8Value(args[1]->ToString()),
       sizeof(asyncData->group)-1);
 

#ifdef DEBUG
printf("spread_leave(%d,%s)\n", asyncData->mbox, asyncData->group);
#endif

    // pass the work token to libuv to be run when a
    // worker-thread is available to
    uv_queue_work(
       uv_default_loop(),
       req, // work token
       Leave_AsyncWork, // work function
       (uv_after_work_cb) Leave_AsyncAfter // function to run when complete
    );

    return scope.Close(Undefined());
} 






/////////////////  Join /////////////////////////


void Join_AsyncWork(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;


#ifdef DEBUG
    int error = SP_join( asyncData->mbox, asyncData->group );
printf("SP_join called -> %d\n", error);
#else
    SP_join( asyncData->mbox, asyncData->group );
#endif
}

void Join_AsyncAfter(uv_work_t *req) {
    AsyncData *asyncData = (AsyncData *)req->data;

    delete asyncData;
    delete req;
}


Handle<Value> spread_join(const Arguments& args) {
    
    HandleScope scope;

#ifdef DEBUG
printf("entering spread_join\n");
#endif

    // create an async work token
    uv_work_t *req = new uv_work_t;

    // assign our data structure that will be passed around
    AsyncData *asyncData = new AsyncData;
    req->data = asyncData;
    
    memset(asyncData,0,sizeof(AsyncData));

    asyncData->mbox = args[0]->Uint32Value();
    strncpy(asyncData->group,
       *String::Utf8Value(args[1]->ToString()),
       sizeof(asyncData->group)-1);
 

#ifdef DEBUG
printf("spread_join(%d,%s)\n", asyncData->mbox, asyncData->group);
#endif

    // pass the work token to libuv to be run when a
    // worker-thread is available to
    uv_queue_work(
       uv_default_loop(),
       req, // work token
       Join_AsyncWork, // work function
       (uv_after_work_cb) Join_AsyncAfter // function to run when complete
    );

    return scope.Close(Undefined());
} 






/////////////////   Connect /////////////////////////


/**
    Called within v8's worker then pool. Connect to spread 
    is we can and populate the mbox variable otherwise
    set the error text    
*/
void Connect_AsyncWork(uv_work_t *req) {
    // fetch our data structure
    AsyncData *asyncData = (AsyncData *)req->data;
    int error;
    char private_group[MAX_GROUP_NAME];


#ifdef DEBUG
printf("entering Connect_AsyncWork\n");
#endif

    sprintf(private_group,"p_%s",asyncData->name);
               
    asyncData->mbox = -1;
    // perform blocking operation
    error =  
        SP_connect(
            (const char *) asyncData->daemon, 
            (const char *) asyncData->name, 1, asyncData->membership_msgs, 
            &asyncData->mbox, private_group
        );

#ifdef DEBUG
printf("SP_connect(%s,%s) returned %d\n", asyncData->daemon, asyncData->name, error);
#endif


    // check for errors
    error_check(asyncData, error);
}



// Function to execute when the async work is complete
// this function will be run inside the main event loop
// so it is safe to use V8 again
void Connect_AsyncAfter(uv_work_t *req) {
    HandleScope scope;
    // fetch our data structure
    AsyncData *asyncData = (AsyncData *)req->data;


#ifdef DEBUG
printf("entering Connect_AsyncAfter\n");
#endif

    // create an arguments array for the callback
    Handle<Value> argv[] = {
       Number::New(asyncData->mbox),
       String::New(asyncData->errorMsg)
    };

    // surround in a try/catch for safety
    TryCatch try_catch;

    // execute the callback function
    asyncData->callback->Call(Context::GetCurrent()->Global(), 2, argv);
    if (try_catch.HasCaught())
      node::FatalException(try_catch);
    // dispose the Persistent handle so the callback
    // function can be garbage-collected
    asyncData->callback.Dispose();
    // clean up any memory we allocated
    delete asyncData;
    delete req;
}




Handle<Value> spread_connect(const Arguments& args) {
    
    HandleScope scope;

#ifdef DEBUG
printf("entering spread_connect\n");
#endif

    // create an async work token
    uv_work_t *req = new uv_work_t;

    // assign our data structure that will be passed around
    AsyncData *asyncData = new AsyncData;
    req->data = asyncData;
    
    memset(asyncData,0,sizeof(AsyncData));

    // get the spread daemon name
    strncpy(asyncData->daemon,
       *String::Utf8Value(args[0]->ToString()),
       sizeof(asyncData->daemon)-1);


    strncpy(asyncData->name,
       *String::Utf8Value(args[1]->ToString()),
       sizeof(asyncData->name)-1);

    asyncData->membership_msgs = args[2]->Uint32Value();
 
    asyncData->callback = Persistent<Function>::New(
    Local<Function>::Cast(args[3]));


#ifdef DEBUG
printf("spread_connect(%s,%s)\n", asyncData->daemon, asyncData->name);
#endif

    // pass the work token to libuv to be run when a
    // worker-thread is available to
    uv_queue_work(
       uv_default_loop(),
       req, // work token
       Connect_AsyncWork, // work function
       (uv_after_work_cb) Connect_AsyncAfter // function to run when complete
    );

    return scope.Close(Undefined());
} 



void init(Handle<Object> exports) {
   exports->Set(
     String::NewSymbol("connect"),
     FunctionTemplate::New(spread_connect)->GetFunction()
   );


   exports->Set(
     String::NewSymbol("join"),
     FunctionTemplate::New(spread_join)->GetFunction()
   );


   exports->Set(
     String::NewSymbol("leave"),
     FunctionTemplate::New(spread_leave)->GetFunction()
   );

   exports->Set(
     String::NewSymbol("disconnect"),
     FunctionTemplate::New(spread_disconnect)->GetFunction()
   );

   exports->Set(
     String::NewSymbol("receive"),
     FunctionTemplate::New(spread_receive)->GetFunction()
   );

   exports->Set(
     String::NewSymbol("multicast"),
     FunctionTemplate::New(spread_multicast)->GetFunction()
   );


   exports->Set(
     String::NewSymbol("REGULAR_MESS"),
     Number::New(REGULAR_MESS)
   );

   exports->Set(
     String::NewSymbol("AGREED_MESS"),
     Number::New(AGREED_MESS)
   );

   exports->Set(
     String::NewSymbol("CAUSAL_MESS"),
     Number::New(CAUSAL_MESS)
   );

   exports->Set(
     String::NewSymbol("FIFO_MESS"),
     Number::New(FIFO_MESS)
   );

   exports->Set(
     String::NewSymbol("RELIABLE_MESS"),
     Number::New(RELIABLE_MESS)
   );

   exports->Set(
     String::NewSymbol("UNRELIABLE_MESS"),
     Number::New(UNRELIABLE_MESS)
   );

   exports->Set(
     String::NewSymbol("SAFE_MESS"),
     Number::New(SAFE_MESS)
   );
   exports->Set(
     String::NewSymbol("SELF_DISCARD"),
     Number::New(SELF_DISCARD)
   );

}




NODE_MODULE(spread, init)









