{
   "targets": [
      {
       "target_name": "spread",
       "sources": [
         "src/cspread.cc"
       ],
       "cflags_cc": ["-fPIC"],
       "libraries": [ 
           "-lm","-lrt","-lnsl",
           "/usr/local/lib/libtspread-core.a", 
       ]
      }
   ],
}

