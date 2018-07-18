.. dialplan_application:

********************
Dialplan application
********************

Tiresias
========
Attemps to fingerprinting and recognize the given channel.

This application attempts to detect the given channel's voice is in the list of the given tiresias's context. Simply, call this application after call has been answered(The Tiresias will be answered the call if the call was not answered).

When loaded, tiresias reads tiresias.conf and uses the parameters specified as default values and contexts. Those default values get overwritten when the calling Tiresias with parameters.

Syntax
------

::

  Tiresias(<contaxt name>,<duration>,[tolerance])

* ``context name``: Context name.
* ``duration``: Duration time(milliseconds).
* ``tolerance``: Tolerance score.

Channel variables
-----------------
This application sets the following channel vairables

::

  TIRSTATUS
  TIRFRAMECOUNT
  TIRMATCHCOUNT

* ``TIRSTATUS`` : This is the status of the voice recognition.
    * ``FOUND``: Found the voice fingerprinting info from the context's audio list.
    * ``NOTFOUND``: Could not find the voice fingerprinting info from the context's audio list.
    * ``HANGUP``: The call has been hungup before complete the recognition.
* ``TIRFRAMECOUNT``: This is the value of the given channel's audio frame total count. It sets only when the TIRSTATUS is FOUND.
* ``TIRMATCHCOUNT``: This is the value of matched count. It sets only when the TIRSTATUS is FOUND.
* ``TIRCONTEXT``: This is the context name of the found voice recognition. This sets only when the TIRSTATUS is FOUND.
* ``TIRFILENAME``: This is the file name of the found voice recognition. This sets only when the TIRSTATUS is FOUND.
* ``TIRFILEHASH``: This is the file hash of the found voice recognition. This sets only when the TIRSTATUS is FOUND.
* ``TIRFILEUUID``: This is the file uuid of the found voice recognition. This sets only when the TIRSTATUS is FOUND.

Example
-------

::

  [test_tiresias]
  exten=> s,1,NoOp(test_tiresias)
  same=> n,Answer()
  same=> n,Tiresias(test,3000)
  same=> n,NoOp(${TIRSTATUS})
  same=> n,NoOp(${TIRFRAMECOUNT})
  same=> n,NoOp(${TIRMATCHCOUNT})
  same=> n,NoOp(${TIRCONTEXT})
  same=> n,NoOp(${TIRFILENAME})
  same=> n,NoOp(${TIRFILEHASH})
  same=> n,NoOp(${TIRFILEUUID})
