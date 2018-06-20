.. cli_operation:

*************
Cli operation
*************

tiresias show contexts
======================
Shows registered context list.

::

  Asterisk*CLI>tiresias show contexts
  
Example
-------
::

  saturn*CLI> tiresias show contexts 
  Name                                 Directory
  test                                 /home/pchero/tmp/mp3

tiresias show audios <context name>
===================================
Shows registered audio lists of given context.

::

  Asterisk*CLI>tiresias show audios <context name>
  
Example
-------
::

  saturn*CLI> tiresias show audios test
  Uuid                                 Name                                          Context                              Hash                                
  1915e0f9-4e3c-4b39-880e-3f800551a1e4 Brad-Sucks--Total-Breakdown.mp3               test                                 0c92fa0c828583ed4361418262dd167c    
  12cc871e-8e0a-47d2-b557-47a098329a41 Choc--Eigenvalue-Subspace-Decomposition.mp3   test                                 73747a0bb4f74499a04e8364292e4255    
  36898a56-fa99-4e7e-93a4-459f2d4f0ee1 Josh-Woodward--I-Want-To-Destroy-Something-Be test                                 e626aeabeb3c5d1e9855b71466f8a26c    
  a2b0f19d-db5f-4fd4-bb60-1dbf7c164ab2 Sean-Fournier--Falling-For-You.mp3            test                                 47658e276b7b686a09b34b959142b799    
  bab02a7b-04a5-491f-91d6-2d3f3ac702ff The-Lights-Galaxia--While-She-Sleeps.mp3      test                                 8d166a882948f761006cc5e52bf8c3aa    
  8d14567e-adda-48ce-b014-cf679f389054 demo-congrats.wav                             test                                 c79c70d62dd82a81f25a02d615f3038c    
  2b88724b-b9d6-4f77-a76c-5feada725499 ibk_ars.wav                                   test                                 cc4ae6223722befe7c0673db37323cf3    
  a4b8a63d-56e7-42ab-9582-94cf668039bf ibk_ars_2.wav                                 test                                 ac04282209539c8833bf5204d9dc4db6    
  1efca21b-9807-4451-802d-0a4bdaf08c37 ibk_ars_3.wav                                 test                                 2bb7ed60e6f28b3c82ceb8df869d631c    


tiresias remove audio <audio uuid>
===================================
Remove the given audio info.

::

  Asterisk*CLI>tiresias remove audio <audio uuid>
  
Example
-------
::

  saturn*CLI> tiresias remove audio bab02a7b-04a5-491f-91d6-2d3f3ac702ff
  Removed the audio info. uuid[bab02a7b-04a5-491f-91d6-2d3f3ac702ff]

