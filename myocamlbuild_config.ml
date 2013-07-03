(* #MYCC=gcc-4.8 *)
(* #MYCFLAGS = -Wall -Wno-unused-value -Wno-div-by-zero -Werror -Wno-error=unused-but-set-variable -g3 -Og -fstack-check *)

(* #MYCFLAGS += -fstack-check *)
let mycflags  = " -Wall -Wno-unused-value -Wno-div-by-zero -Werror -Wno-error=unused-but-set-variable -Wno-error=strict-aliasing -g3 -O2";;
(* #MYCFLAGS = -Ofast *)

(* #MYCFLAGS = -Wall -Wno-unused-value -Wno-div-by-zero -Werror -Wno-error=unused-but-set-variable -g3 -O1 *)
let mycc = "gcc-4.8 "^mycflags;;

(* # CFLAGS += "^mycflags^" *)
(* # MYCFLAGS += -g *)
(* # MYCFLAGS += -O1 *)

(* # This is very likely crap: *)
(* #MYCFLAGS += -DDEBUG -D_DEBUG *)
(* #MYCFLAGS += -mthreads *)

(* # generated by ./configure --prefix /home/luca/usr-patched-ocaml/ --with-debug-runtime *)
let prefix = "/home/luca/usr-patched-ocaml/";;
let bindir = prefix^"/bin";;
let libdir = prefix^"/lib/ocaml";;
let stublibdir = libdir^"/stublibs";;
let mandir = prefix^"/man";;
let manext = "1";;
let ranlib = "ranlib";;
let ranlibcmd = "ranlib";;
let arcmd = "ar";;
let sharpbangscripts = true;;
let bng_arch = "amd64";;
let bng_asm_level = "1";;
let pthread_link = "-cclib -lpthread";;
let x11_includes = "not found";;
let x11_link = "not found";;
let tk_defs = "";;
let tk_link = "";;
let libbfd_link = "-lbfd -ldl -liberty -lz";;
let bytecc = mycc^" "^mycflags;;
let bytecccompopts = "-fno-defer-pop "^mycflags^" -D_FILE_OFFSET_BITS=64 -D_REENTRANT";;
let bytecclinkopts = " -Wl,-E";;
let bytecclibs = " -lm  -ldl -lcurses -lpthread";;
let byteccrpath = "-Wl,-rpath,";;
let exe = "";;
let supports_shared_libraries = true;;
let sharedcccompopts = "-fPIC";;
let mksharedlibrpath = "-Wl,-rpath,";;
let natdynlinkopts = "-Wl,-E";;
(* SYSLIB=-l"^1^" *)
let syslib x = "-l"^x;;

(* ## *)
(* MKLIB=ar rc "^1^" "^2^"; ranlib "^1^" *)
let mklib out files opts = Printf.sprintf "ar rc %s %s %s; ranlib %s" out opts files out;;
let arch = "amd64";;
let model = "default";;
let system = "linux";;
let nativecc = mycc^" "^mycflags;;
let nativecccompopts = mycflags^" -D_FILE_OFFSET_BITS=64 -D_REENTRANT";;
let nativeccprofopts = mycflags^" -D_FILE_OFFSET_BITS=64 -D_REENTRANT";;
let nativecclinkopts = "";;
let nativeccrpath = "-Wl,-rpath,";;
let nativecclibs = " -lm  -ldl -lpthread";;
let asm = "as";;
let aspp = mycc^" "^mycflags^" -c";;
let asppprofflags = "-DPROFILING";;
let profiling = "prof";;
let dynlinkopts = " -ldl";;
let otherlibraries = "unix str num dynlink bigarray systhreads";;
let debugger = "ocamldebugger";;
let cc_profile = "-pg";;
let systhread_support = true;;
let partialld = "ld -r";;
let packld = partialld^" "^nativecclinkopts^" -o\ ";;
let dllcccompopts = "";;

let o = "o";;
let a = "a";;
let so = "so";;
let ext_obj = ".o";;
let ext_asm = ".s";;
let ext_lib = ".a";;
let ext_dll = ".so";;
let extralibs = "";;
let ccomptype = mycc^" "^mycflags;;
let toolchain = mycc^" "^mycflags;;
let natdynlink = true;;
let cmxs = "cmxs";;
let mkexe = bytecc;;
let mkexedebugflag = "-g";;
let mkdll = mycc^" "^mycflags^" -shared";;
let mkmaindll = mycc^" "^mycflags^" -shared";;
let runtimed = "runtimed";;
let camlp4 = "camlp4";;
let asm_cfi_supported = true;;
