NODE-CSPREAD


Overview:

     This is a C++ addon to nodejs to interface with the spread C API to provide
a high performance binding to the spread toolkit (see www.spread.org). It contains a thin abstraction called SpreadSession which provides an event based interface where the end user supplies a set of callbacks to handle various events. 


Build & Install:

     You must first have the spread toolkit built and installed. The binding.gpy expects the
spread to be installed in the /usr/local/lib directory. Next you can build the addon by doing:

cd node-cspread
node-gyp build
sudo node-gyp install

Examples:

   See the unittest function in spread.js as a usage example.

API:

SpreadSession( 
    daemon_address /* 4803@localhost*/,
    uniqueName /* system wide unique name like a  UUID */,
    success /* callback function executed when connected */,
    failure  /* called if we can't connect */
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

  



