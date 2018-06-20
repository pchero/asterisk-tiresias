.. configuration

*************
Configuration
*************
The tiresias.conf consists of 2 parts. One is global, and the other is context.

/etc/tiresias.conf
::

  [global]
  tolerance=0.001

  [mycontext]
  directory=/home/pchero/tmp/wav


global
======
The global section holds global configuration informaton. It affects to the all other context options as default values.

Items
-----

::

  tolerance

* tolerance: Gives flexible range for the audio fingerprint matching. If the gives more tolerance, it will returns more matching count, but less accuracy.

context
=======

Items
-----

::

  directory

* directory: Context's audio file directory. The tiresias will fingerprinting and store it into the database, all of files in this directory.


