(* camlp4r pa_extend.cmo q_MLast.cmo *)
(* This file has been generated by program: do not edit! *)

type 'a item_or_def = SdStr of 'a | SdDef of string | SdUnd of string | SdNop
;;

let list_remove x l =
  List.fold_right (fun e l -> if e = x then l else e :: l) l []
;;

let defined = ref ["OCAML_305"; "CAMLP4_300"; "NEWSEQ"];;
let define x = defined := x :: !defined;;
let undef x = defined := list_remove x !defined;;

Grammar.extend
  (let _ = (Pcaml.expr : 'Pcaml__expr Grammar.Entry.e)
   and _ = (Pcaml.str_item : 'Pcaml__str_item Grammar.Entry.e)
   and _ = (Pcaml.sig_item : 'Pcaml__sig_item Grammar.Entry.e) in
   let grammar_entry_create s =
     Grammar.Entry.create (Grammar.of_entry Pcaml.expr) s
   in
   let def_undef_str : 'def_undef_str Grammar.Entry.e =
     grammar_entry_create "def_undef_str"
   and str_item_def_undef : 'str_item_def_undef Grammar.Entry.e =
     grammar_entry_create "str_item_def_undef"
   and def_undef_sig : 'def_undef_sig Grammar.Entry.e =
     grammar_entry_create "def_undef_sig"
   and sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e =
     grammar_entry_create "sig_item_def_undef"
   in
   [Grammar.Entry.obj (Pcaml.expr : 'Pcaml__expr Grammar.Entry.e),
    Some (Gramext.Level "top"),
    [None, None,
     [[Gramext.Stoken ("", "ifndef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then"); Gramext.Sself;
       Gramext.Stoken ("", "else"); Gramext.Sself],
      Gramext.action
        (fun (e2 : 'Pcaml__expr) _ (e1 : 'Pcaml__expr) _ (c : string) _
           (loc : int * int) ->
           (if List.mem c !defined then e2 else e1 : 'Pcaml__expr));
      [Gramext.Stoken ("", "ifdef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then"); Gramext.Sself;
       Gramext.Stoken ("", "else"); Gramext.Sself],
      Gramext.action
        (fun (e2 : 'Pcaml__expr) _ (e1 : 'Pcaml__expr) _ (c : string) _
           (loc : int * int) ->
           (if List.mem c !defined then e1 else e2 : 'Pcaml__expr))]];
    Grammar.Entry.obj (Pcaml.str_item : 'Pcaml__str_item Grammar.Entry.e),
    Some Gramext.First,
    [None, None,
     [[Gramext.Snterm
         (Grammar.Entry.obj
            (def_undef_str : 'def_undef_str Grammar.Entry.e))],
      Gramext.action
        (fun (x : 'def_undef_str) (loc : int * int) ->
           (match x with
              SdStr si -> si
            | SdDef x -> define x; MLast.StDcl (loc, [])
            | SdUnd x -> undef x; MLast.StDcl (loc, [])
            | SdNop -> MLast.StDcl (loc, []) :
            'Pcaml__str_item))]];
    Grammar.Entry.obj (def_undef_str : 'def_undef_str Grammar.Entry.e), None,
    [None, None,
     [[Gramext.Stoken ("", "undef"); Gramext.Stoken ("UIDENT", "")],
      Gramext.action
        (fun (c : string) _ (loc : int * int) -> (SdUnd c : 'def_undef_str));
      [Gramext.Stoken ("", "define"); Gramext.Stoken ("UIDENT", "")],
      Gramext.action
        (fun (c : string) _ (loc : int * int) -> (SdDef c : 'def_undef_str));
      [Gramext.Stoken ("", "ifndef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e1 : 'str_item_def_undef) _ (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then SdNop else e1 : 'def_undef_str));
      [Gramext.Stoken ("", "ifndef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e));
       Gramext.Stoken ("", "else");
       Gramext.Snterm
         (Grammar.Entry.obj
            (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e2 : 'str_item_def_undef) _ (e1 : 'str_item_def_undef) _
           (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then e2 else e1 : 'def_undef_str));
      [Gramext.Stoken ("", "ifdef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e1 : 'str_item_def_undef) _ (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then e1 else SdNop : 'def_undef_str));
      [Gramext.Stoken ("", "ifdef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e));
       Gramext.Stoken ("", "else");
       Gramext.Snterm
         (Grammar.Entry.obj
            (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e2 : 'str_item_def_undef) _ (e1 : 'str_item_def_undef) _
           (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then e1 else e2 : 'def_undef_str))]];
    Grammar.Entry.obj
      (str_item_def_undef : 'str_item_def_undef Grammar.Entry.e),
    None,
    [None, None,
     [[Gramext.Snterm
         (Grammar.Entry.obj
            (Pcaml.str_item : 'Pcaml__str_item Grammar.Entry.e))],
      Gramext.action
        (fun (si : 'Pcaml__str_item) (loc : int * int) ->
           (SdStr si : 'str_item_def_undef));
      [Gramext.Snterm
         (Grammar.Entry.obj
            (def_undef_str : 'def_undef_str Grammar.Entry.e))],
      Gramext.action
        (fun (d : 'def_undef_str) (loc : int * int) ->
           (d : 'str_item_def_undef))]];
    Grammar.Entry.obj (Pcaml.sig_item : 'Pcaml__sig_item Grammar.Entry.e),
    Some Gramext.First,
    [None, None,
     [[Gramext.Snterm
         (Grammar.Entry.obj
            (def_undef_sig : 'def_undef_sig Grammar.Entry.e))],
      Gramext.action
        (fun (x : 'def_undef_sig) (loc : int * int) ->
           (match x with
              SdStr si -> si
            | SdDef x -> define x; MLast.SgDcl (loc, [])
            | SdUnd x -> undef x; MLast.SgDcl (loc, [])
            | SdNop -> MLast.SgDcl (loc, []) :
            'Pcaml__sig_item))]];
    Grammar.Entry.obj (def_undef_sig : 'def_undef_sig Grammar.Entry.e), None,
    [None, None,
     [[Gramext.Stoken ("", "undef"); Gramext.Stoken ("UIDENT", "")],
      Gramext.action
        (fun (c : string) _ (loc : int * int) -> (SdUnd c : 'def_undef_sig));
      [Gramext.Stoken ("", "define"); Gramext.Stoken ("UIDENT", "")],
      Gramext.action
        (fun (c : string) _ (loc : int * int) -> (SdDef c : 'def_undef_sig));
      [Gramext.Stoken ("", "ifndef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e1 : 'sig_item_def_undef) _ (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then SdNop else e1 : 'def_undef_sig));
      [Gramext.Stoken ("", "ifndef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e));
       Gramext.Stoken ("", "else");
       Gramext.Snterm
         (Grammar.Entry.obj
            (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e2 : 'sig_item_def_undef) _ (e1 : 'sig_item_def_undef) _
           (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then e2 else e1 : 'def_undef_sig));
      [Gramext.Stoken ("", "ifdef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e1 : 'sig_item_def_undef) _ (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then e1 else SdNop : 'def_undef_sig));
      [Gramext.Stoken ("", "ifdef"); Gramext.Stoken ("UIDENT", "");
       Gramext.Stoken ("", "then");
       Gramext.Snterm
         (Grammar.Entry.obj
            (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e));
       Gramext.Stoken ("", "else");
       Gramext.Snterm
         (Grammar.Entry.obj
            (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e))],
      Gramext.action
        (fun (e2 : 'sig_item_def_undef) _ (e1 : 'sig_item_def_undef) _
           (c : string) _ (loc : int * int) ->
           (if List.mem c !defined then e1 else e2 : 'def_undef_sig))]];
    Grammar.Entry.obj
      (sig_item_def_undef : 'sig_item_def_undef Grammar.Entry.e),
    None,
    [None, None,
     [[Gramext.Snterm
         (Grammar.Entry.obj
            (Pcaml.sig_item : 'Pcaml__sig_item Grammar.Entry.e))],
      Gramext.action
        (fun (si : 'Pcaml__sig_item) (loc : int * int) ->
           (SdStr si : 'sig_item_def_undef));
      [Gramext.Snterm
         (Grammar.Entry.obj
            (def_undef_sig : 'def_undef_sig Grammar.Entry.e))],
      Gramext.action
        (fun (d : 'def_undef_sig) (loc : int * int) ->
           (d : 'sig_item_def_undef))]]]);;

Pcaml.add_option "-D" (Arg.String define)
  "<string>   Define for ifdef instruction.";;
Pcaml.add_option "-U" (Arg.String undef)
  "<string>   Undefine for ifdef instruction.";;
