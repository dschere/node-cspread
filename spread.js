/**
NODE-CSPREAD


Overview:

     This is a C++ addon to nodejs to interface with the spread C API to provide
a high performance binding to the spread toolkit (see www.spread.org). It contains a thin abstraction called SpreadSession which provides an event based interface where the end user supplies a set of callbacks to handle various events. 


Build & Install:

     You must first have the spread toolkit built and installed. If you are on 
a 64 bit Linux system be sure to use ./configure --with-pic when configuring and
installing spread or else you will get linker errors.

The binding.gpy expects the spread to be installed in the /usr/local/lib directory. 
Next you can build the addon by doing:

cd node-cspread
node-gyp build
sudo node-gyp install

Examples:

   See the unittest function in spread.js as a usage example.

API:

SpreadSession( 
    daemon_address // 4803@localhost,
    uniqueName // system wide unique name like a  UUID ,
    success // callback function executed when connected ,
    failure  // called if we can't connect 
); 

If successful in connecting to the spread daemon, the success function
above is called with an instance of a spread session object. Inside this
function the user can join message groups and setup callbacks to handle 
events:




Example:

function success( session ) {
     session.join( “group name” );
     session.dataHandler = function(  group, sender, data ){

           console.log( group + "," + sender + "," + data );
     };
     session.memberJoinedGroup =  function( group, sender ) {
           // detect when a new process has join a group
           // group: name of the spread group 
           // sender: process uniqueName 
     };
     session.memberLeftGroup = function( group, sender ) {
           // detect when a member has left a message group
     };
     
     // these are error conditions
     session.leaveCausedByNetwork = function( group, sender ) {
          // network outage caused a member to leave
     };
     session.leaveCausedByDisconnect = function( group, sender ) {
          // process lost connection to spread (process crash message).
     };
     session.onDisconnect = function( errorText ) {
          // we just lost our connection to spread.
     };   
     session.onError = function( errorText ) {
          // general error event
     };



     // to send a message to other groups
     session.multicast( session.SAFE_MESS, session.SELF_DISCARD,
          “group name”,  JSON.stringify( … some data … ) );


};    
*/
var spread = require('./build/Release/spread');


var SpreadSession = function( daemon, uniqueName, recvMembershipMsgs, onConnected, onFailure )
{
    var session = this;

    this.SAFE_MESS     = spread.SAFE_MESS;
    this.RELIABLE_MESS = spread.RELIABLE_MESS;
    this.SELF_DISCARD  = spread.SELF_DISCARD;

    session.mbox = -1;    

    // override this functions as needed to handle events.
    this.dataHandler = function( group, sender, data )       {
        console.log("dataHandler: group=" + group +
                                 "sender=" + sender +
                                 "data=" + data); 
    };

    this.leaveCausedByNetwork = function( group, sender )    {};
    this.leaveCausedByDisconnect = function( group, sender ) {};
    this.memberLeftGroup = function( group, sender )         {
        console.log( sender + " left " + group ); 
    };
    this.memberJoinedGroup = function( group, sender )       {
        console.log( sender + " joined " + group ); 
    };    

    // error handlers 
    this.onDisconnect = function( errorText ) {
        console.log("disconnected from spread: " + errorText ); 
    };

    this.onFailedToConnect = function( errorText ) {
        console.log("failed to connect to spread: " + errorText ); 
    };

    this.onError = function( errorText ) {
        console.log("error: " + errorText ); 
    };


    ////  These a considered non virtual functions ////////


    this.join = function( group ) {
        if ( session.mbox != -1 ) {
            spread.join( session.mbox, group );  
        } else {
            session.onError( " join() failed no connection" ); 
        }       
    }

    this.leave = function( group ) {
        if ( session.mbox != -1 ) {
            spread.leave( session.mbox, group );  
        } else {
            session.onError( " leave() failed no connection" ); 
        }       
    }

    this.disconnect = function() {
        if ( session.mbox != -1 ) {
            spread.disconnect( session.mbox );  
        }
    };  

    this.multicast = function ( msgDeliveryType, group, data ) {
        if ( session.mbox != -1 ) {
            spread.multicast( 
                session.mbox, 
                msgDeliveryType, 
                group, 
                data,
                function (mb,errorText) {
                    session.mbox = mb; 
                    if ( session.mbox == -1 )
                    {
                        session.onDisconnect(errorText); 
                    } 
                    else
                    {
                        session.onError(errorText);
                    } 
                } 
            );
        } else {
            session.onError( "multicast() failed no connection" ); 
        }
    }; 


    this.event_loop = function( mb, jsonText, data ) {
         json = JSON.parse( jsonText );
         console.log( json );
         switch( json.eventType )
         {
             case "JOIN":
                 session.memberJoinedGroup( json.group, json.sender );
                 break;
             case "LEAVE":
                 session.memberLeftGroup( json.group, json.sender );
                 break;
             case "DISCONNECT":
                 session.leaveCausedByDisconnect( json.group, json.sender );
                 break;
             case "NETWORK":
                 session.leaveCausedByNetwork( json.group, json.sender );
                 break;
             case "DATA":
                 session.dataHandler( json.group, json.sender, data );
                 break;
             case "ERROR":
                 if ( mb == -1 ) {
                     session.onDisconnect( json.errorMsg );
                 } else {    
                     session.onError( json.errorMsg );
                 }
                 break; 
         }

         spread.receive( mb, session.event_loop );
    }; 

    spread.connect( daemon, uniqueName, recvMembershipMsgs, function(mb,errText) {
         session.mbox = mb;
         if ( mb == -1 ) {
             onFailure( errText );
         } else {
             onConnected( this );
             spread.receive( mb, session.event_loop );  
         }      
    });


    return this;
}




function unittest() {


/**

Test join,leave,multicast functions. Setup a 
process group with two connections were connection A
sends even numbers and connection B sends odd numbers
counting upto 100. 

*/


 function odd( session ) {
    
    session.join("odd");

    session.dataHandler = 
       function ( group, sender, data ){
           console.log( group + "," + sender + "," + data );

           console.log( "odd received:" + JSON.parse(data).message ); 
       };

    session.memberJoinedGroup = 
       function(group, member){
           for(var i=0; i < 100; i += 2 ){
               data = JSON.stringify({
                   message: i
               });
               session.multicast( 
                  session.SAFE_MESS | session.SELF_DISCARD,
                  "even",
                  data
               );
           }
       };    
      
 }


 function even( session ) {

    session.join("even");

    session.dataHandler = 
       function ( group, sender, data ){
           console.log( group + "," + sender + "," + data );
           console.log( "even received:" + JSON.parse(data).message ); 
       };

    session.memberJoinedGroup = 
       function(group, member){
           for(var i=1; i < 100; i += 2 ){
               data = JSON.stringify({
                   message: i
               });
               session.multicast( 
                  session.SAFE_MESS | session.SELF_DISCARD,
                  "odd",
                  data
               );
           }
       };    
 }

 function connect_failed( err ) {
     console.log( err );   
 }

 var sp1 = SpreadSession( "4803@localhost", "proc1", 1, odd, connect_failed );
 var sp2 = SpreadSession( "4803@localhost", "proc2", 1, even, connect_failed );


}//end unit test


//unittest();
module.exports = exports = SpreadSession;

