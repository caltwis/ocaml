(* Luca Saiu, REENTRANTRUNTIME *)

(* Basic context operations*)
type t

val self : unit -> t
val is_main : t -> bool
(* val is_alive : t -> bool *)


(* Mailboxes *)
type mailbox

val make_mailbox : unit -> mailbox

val context_of_mailbox : mailbox -> t
val is_mailbox_local : mailbox -> bool

val split1 : (mailbox -> unit) -> (*new context mailbox*)mailbox
val split : int -> (int -> mailbox -> unit) -> (*mailboxes to new contexts*)(mailbox list)

val send : mailbox -> 'a -> unit
val receive : mailbox -> 'a (* raises ForeignMailbox if the mailbox is foreign *)

(* Wait until the context local to the given mailbox or mailboxes terminates: *)
(* FIXME: fix the multi-thread case *)
val join_context : t -> unit 
val join_contexts : t list -> unit
val join1 : mailbox -> unit
val join : mailbox list -> unit


(* Handlers -- an event interface *)

(* (\* Create a context which will only execute handlers. *\) *)
(* val make_handler_context : unit -> mailbox *)

(* Add a handler, to be executed *)
(* val register_handler : (mailbox -> unit) -> unit *)


(* Utility *)

(* Return the total number of CPUs in the system, counting each core or
   similar element as one unit: *)
val cpu_no : unit -> int


(* Scratch.  Horrible things which are only useful for debugging. *)

val to_string : t -> string
val sself : unit -> string

(* FIXME: these are for debugging only, and global_index in particular
   is not exactly type-safe :-) *)
val globals : unit -> 'a
(* val globals_and_datum : 'a -> ('b * 'a) *)

(* Return the index for the given global value (compared by identity)
   within the array of all globals.  Raise an exception or crash
   horribly if the given value does not correspond to any global. *)
val global_index : 'a -> int

(* val dump : unit -> int *)
