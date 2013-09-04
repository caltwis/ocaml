/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* Structured output */

/* The interface of this file is "intext.h" */

#define CAML_CONTEXT_STARTUP
#define CAML_CONTEXT_EXTERN
#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_MEMORY

#include <stdio.h>
#include <string.h>
#include "alloc.h"
#include "custom.h"
#include "fail.h"
#include "gc.h"
#include "intext.h"
#include "io.h"
#include "md5.h"
#include "memory.h"
#include "misc.h"
#include "mlvalues.h"
#include "reverse.h"

// !!!!!!!!!!!!!!!
void dump_digest(unsigned char *digest){
  char msg[256];
  sprintf(msg, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
          digest[0], digest[1], digest[2], digest[3],
          digest[4], digest[5], digest[6], digest[7],
          digest[8], digest[9], digest[10], digest[11],
          digest[12], digest[13], digest[14], digest[15]);
  INIT_CAML_R;
  DUMP("dumping %s", msg);
}


/* Forward declarations */

static void extern_out_of_memory_r(CAML_R);
static void extern_invalid_argument_r(CAML_R, char *msg);
static void extern_failwith_r(CAML_R, char *msg);
static void extern_stack_overflow_r(CAML_R);
static struct code_fragment * extern_find_code_r(CAML_R, char *addr);
static void extern_replay_trail_r(CAML_R);
static void free_extern_output_r(CAML_R);

/* Free the extern stack if needed */
static void extern_free_stack_r(CAML_R)
{
  if (extern_stack != extern_stack_init) {
    free(extern_stack);
    /* Reinitialize the globals for next time around */
    extern_stack = extern_stack_init;
    extern_stack_limit = extern_stack + EXTERN_STACK_INIT_SIZE;
  }
}

static struct extern_item * extern_resize_stack_r(CAML_R, struct extern_item * sp)
{
  asize_t newsize = 2 * (extern_stack_limit - extern_stack);
  asize_t sp_offset = sp - extern_stack;
  struct extern_item * newstack;

  if (newsize >= EXTERN_STACK_MAX_SIZE) extern_stack_overflow_r(ctx);
  if (extern_stack == extern_stack_init) {
    newstack = malloc(sizeof(struct extern_item) * newsize);
    if (newstack == NULL) extern_stack_overflow_r(ctx);
    memcpy(newstack, extern_stack_init,
           sizeof(struct extern_item) * EXTERN_STACK_INIT_SIZE);
  } else {
    newstack =
      realloc(extern_stack, sizeof(struct extern_item) * newsize);
    if (newstack == NULL) extern_stack_overflow_r(ctx);
  }
  extern_stack = newstack;
  extern_stack_limit = newstack + newsize;
  return newstack + sp_offset;
}

/* Initialize the trail */

static void init_extern_trail_r(CAML_R)
{
  extern_trail_block = &extern_trail_first;
  extern_trail_cur = extern_trail_block->entries;
  extern_trail_limit = extern_trail_block->entries + ENTRIES_PER_TRAIL_BLOCK;
}

/* Replay the trail, undoing the in-place modifications
   performed on objects */

static void extern_replay_trail_r(CAML_R)
{
  struct trail_block * blk, * prevblk;
  struct trail_entry * ent, * lim;

  blk = extern_trail_block;
  lim = extern_trail_cur;
  while (1) {
    for (ent = &(blk->entries[0]); ent < lim; ent++) {
      value obj = ent->obj;
      color_t colornum = obj & 3;
      obj = obj & ~3;
      Hd_val(obj) = Coloredhd_hd(Hd_val(obj), colornum);
      Field(obj, 0) = ent->field0;
    }
    if (blk == &extern_trail_first) break;
    prevblk = blk->previous;
    free(blk);
    blk = prevblk;
    lim = &(blk->entries[ENTRIES_PER_TRAIL_BLOCK]);
  }
  /* Protect against a second call to extern_replay_trail */
  extern_trail_block = &extern_trail_first;
  extern_trail_cur = extern_trail_block->entries;
}

/* Set forwarding pointer on an object and add corresponding entry
   to the trail. */

static void extern_record_location_r(CAML_R, value obj)
{
  header_t hdr;

  //printf("FFFFFFFFFF 100\n");
  if (extern_ignore_sharing) return;
  //printf("FFFFFFFFFF 200\n");
  if (extern_trail_cur == extern_trail_limit) {
    struct trail_block * new_block = malloc(sizeof(struct trail_block));
    if (new_block == NULL) extern_out_of_memory_r(ctx);
    new_block->previous = extern_trail_block;
    extern_trail_block = new_block;
    extern_trail_cur = extern_trail_block->entries;
    extern_trail_limit = extern_trail_block->entries + ENTRIES_PER_TRAIL_BLOCK;
  }
  //printf("FFFFFFFFFF 300\n");
  hdr = Hd_val(obj);
  extern_trail_cur->obj = obj | Colornum_hd(hdr);
  extern_trail_cur->field0 = Field(obj, 0);
  extern_trail_cur++;
  //printf("FFFFFFFFFF 500\n");
  Hd_val(obj) = Bluehd_hd(hdr);
  Field(obj, 0) = (value) obj_counter;
  obj_counter++;
  //printf("FFFFFFFFFF 1000\n");
}

/* To buffer the output */

static void init_extern_output_r(CAML_R)
{
  extern_userprovided_output = NULL;
  extern_output_first = malloc(sizeof(struct output_block));
  if (extern_output_first == NULL) caml_raise_out_of_memory_r(ctx);
  extern_output_block = extern_output_first;
  extern_output_block->next = NULL;
  extern_ptr = extern_output_block->data;
  extern_limit = extern_output_block->data + SIZE_EXTERN_OUTPUT_BLOCK;
}

static void close_extern_output_r(CAML_R)
{
  if (extern_userprovided_output == NULL){
    extern_output_block->end = extern_ptr;
  }
}

static void free_extern_output_r(CAML_R)
{
  struct output_block * blk, * nextblk;

  if (extern_userprovided_output != NULL) return;
  for (blk = extern_output_first; blk != NULL; blk = nextblk) {
    nextblk = blk->next;
    free(blk);
  }
  extern_output_first = NULL;
  extern_free_stack_r(ctx);
}

static void grow_extern_output_r(CAML_R, intnat required)
{
  struct output_block * blk;
  intnat extra;

  if (extern_userprovided_output != NULL) {
    extern_failwith_r(ctx, "Marshal.to_buffer: buffer overflow");
  }
  extern_output_block->end = extern_ptr;
  if (required <= SIZE_EXTERN_OUTPUT_BLOCK / 2)
    extra = 0;
  else
    extra = required;
  blk = malloc(sizeof(struct output_block) + extra);
  if (blk == NULL) extern_out_of_memory_r(ctx);
  extern_output_block->next = blk;
  extern_output_block = blk;
  extern_output_block->next = NULL;
  extern_ptr = extern_output_block->data;
  extern_limit = extern_output_block->data + SIZE_EXTERN_OUTPUT_BLOCK + extra;
}

static intnat extern_output_length_r(CAML_R)
{
  struct output_block * blk;
  intnat len;

  if (extern_userprovided_output != NULL) {
    return extern_ptr - extern_userprovided_output;
  } else {
    for (len = 0, blk = extern_output_first; blk != NULL; blk = blk->next)
      len += blk->end - blk->data;
    return len;
  }
}

/* Exception raising, with cleanup */

static void extern_out_of_memory_r(CAML_R)
{
  extern_replay_trail_r(ctx);
  free_extern_output_r(ctx);
  caml_raise_out_of_memory_r(ctx);
}

static void extern_invalid_argument_r(CAML_R, char *msg)
{
  extern_replay_trail_r(ctx);
  free_extern_output_r(ctx);
  caml_invalid_argument_r(ctx, msg);
}

static void extern_failwith_r(CAML_R, char *msg)
{
  extern_replay_trail_r(ctx);
  free_extern_output_r(ctx);
  caml_failwith_r(ctx, msg);
}

static void extern_stack_overflow_r(CAML_R)
{
  caml_gc_message (0x04, "Stack overflow in marshaling value\n", 0);
  extern_replay_trail_r(ctx);
  free_extern_output_r(ctx);
  caml_raise_out_of_memory_r(ctx);
}

/* Write characters, integers, and blocks in the output buffer */

#define Write(c) \
  if (extern_ptr >= extern_limit) grow_extern_output_r(ctx, 1);  \
  *extern_ptr++ = (c)

static void writeblock_r(CAML_R, char *data, intnat len)
{
  if (extern_ptr + len > extern_limit) grow_extern_output_r(ctx, len);
  memmove(extern_ptr, data, len);
  extern_ptr += len;
}

#if ARCH_FLOAT_ENDIANNESS == 0x01234567 || ARCH_FLOAT_ENDIANNESS == 0x76543210
#define writeblock_float8(data,ndoubles) \
  writeblock_r(ctx, (char *)(data), (ndoubles) * 8)
#else
#define writeblock_float8(data,ndoubles) \
  caml_serialize_block_float_8((data), (ndoubles))
#endif

static void writecode8_r(CAML_R, int code, intnat val)
{
  if (extern_ptr + 2 > extern_limit) grow_extern_output_r(ctx, 2);
  extern_ptr[0] = code;
  extern_ptr[1] = val;
  extern_ptr += 2;
}

static void writecode16_r(CAML_R, int code, intnat val)
{
  if (extern_ptr + 3 > extern_limit) grow_extern_output_r(ctx, 3);
  extern_ptr[0] = code;
  extern_ptr[1] = val >> 8;
  extern_ptr[2] = val;
  extern_ptr += 3;
}

static void write32_r(CAML_R, intnat val)
{
  if (extern_ptr + 4 > extern_limit) grow_extern_output_r(ctx, 4);
  extern_ptr[0] = val >> 24;
  extern_ptr[1] = val >> 16;
  extern_ptr[2] = val >> 8;
  extern_ptr[3] = val;
  extern_ptr += 4;
}

static void writecode32_r(CAML_R, int code, intnat val)
{
  if (extern_ptr + 5 > extern_limit) grow_extern_output_r(ctx, 5);
  extern_ptr[0] = code;
  extern_ptr[1] = val >> 24;
  extern_ptr[2] = val >> 16;
  extern_ptr[3] = val >> 8;
  extern_ptr[4] = val;
  extern_ptr += 5;
}

#ifdef ARCH_SIXTYFOUR
static void writecode64_r(CAML_R, int code, intnat val)
{
  int i;
  if (extern_ptr + 9 > extern_limit) grow_extern_output_r(ctx, 9);
  *extern_ptr ++ = code;
  for (i = 64 - 8; i >= 0; i -= 8) *extern_ptr++ = val >> i;
}
#endif

/* Marshal the given value in the output buffer */

static void extern_rec_r(CAML_R, value v)
{
  struct code_fragment * cf;
  struct extern_item * sp;
  sp = extern_stack;

  while(1) {
    //?????DUMP("QQQ 0x%lx, or %li ", v, v);
  if (Is_long(v)) {
    intnat n = Long_val(v);
    if (n >= 0 && n < 0x40) {
      Write(PREFIX_SMALL_INT + n);
    } else if (n >= -(1 << 7) && n < (1 << 7)) {
      writecode8_r(ctx, CODE_INT8, n);
    } else if (n >= -(1 << 15) && n < (1 << 15)) {
      writecode16_r(ctx, CODE_INT16, n);
#ifdef ARCH_SIXTYFOUR
    } else if (n < -((intnat)1 << 31) || n >= ((intnat)1 << 31)) {
      writecode64_r(ctx, CODE_INT64, n);
#endif
    } else
      writecode32_r(ctx, CODE_INT32, n);
    goto next_item;
  }
  if (Is_in_value_area(v)) {
    header_t hd = Hd_val(v);
    tag_t tag = Tag_hd(hd);
    mlsize_t sz = Wosize_hd(hd);
    //DUMP("dumping %p, tag %i, size %i", (void*)v, (int)tag, (int)sz); // !!!!!!!!!!!!!!!
    if (tag == Forward_tag) {
      value f = Forward_val (v);
      if (Is_block (f)
          && (!Is_in_value_area(f) || Tag_val (f) == Forward_tag
              || Tag_val (f) == Lazy_tag || Tag_val (f) == Double_tag)){
        /* Do not short-circuit the pointer. */
      }else{
        v = f;
        continue;
      }
    }
    /* Atoms are treated specially for two reasons: they are not allocated
       in the externed block, and they are automatically shared. */
    if (sz == 0) {
      if (tag < 16) {
        Write(PREFIX_SMALL_BLOCK + tag);
      } else {
        writecode32_r(ctx, CODE_BLOCK32, hd);
      }
      goto next_item;
    }
    /* Check if already seen */
    if (Color_hd(hd) == Caml_blue) {
      uintnat d = obj_counter - (uintnat) Field(v, 0);
      if (d < 0x100) {
        writecode8_r(ctx, CODE_SHARED8, d);
      } else if (d < 0x10000) {
        writecode16_r(ctx, CODE_SHARED16, d);
      } else {
        writecode32_r(ctx, CODE_SHARED32, d);
      }
      goto next_item;
    }

    /* Output the contents of the object */
    switch(tag) {
    case String_tag: {
      mlsize_t len = caml_string_length(v);
      if (len < 0x20) {
        Write(PREFIX_SMALL_STRING + len);
      } else if (len < 0x100) {
        writecode8_r(ctx, CODE_STRING8, len);
      } else {
        writecode32_r(ctx, CODE_STRING32, len);
      }
      writeblock_r(ctx, String_val(v), len);
      size_32 += 1 + (len + 4) / 4;
      size_64 += 1 + (len + 8) / 8;
      extern_record_location_r(ctx, v);
      break;
    }
    case Double_tag: {
      if (sizeof(double) != 8)
        extern_invalid_argument_r(ctx, "output_value: non-standard floats");
      Write(CODE_DOUBLE_NATIVE);
      writeblock_float8((double *) v, 1);
      size_32 += 1 + 2;
      size_64 += 1 + 1;
      extern_record_location_r(ctx,v);
      break;
    }
    case Double_array_tag: {
      mlsize_t nfloats;
      if (sizeof(double) != 8)
        extern_invalid_argument_r(ctx, "output_value: non-standard floats");
      nfloats = Wosize_val(v) / Double_wosize;
      if (nfloats < 0x100) {
        writecode8_r(ctx, CODE_DOUBLE_ARRAY8_NATIVE, nfloats);
      } else {
        writecode32_r(ctx, CODE_DOUBLE_ARRAY32_NATIVE, nfloats);
      }
      writeblock_float8((double *) v, nfloats);
      size_32 += 1 + nfloats * 2;
      size_64 += 1 + nfloats;
      extern_record_location_r(ctx, v);
      break;
    }
    case Abstract_tag:
      extern_invalid_argument_r(ctx, "output_value: abstract value (Abstract)");
      break;
    case Infix_tag:
      writecode32_r(ctx,CODE_INFIXPOINTER, Infix_offset_hd(hd));
      extern_rec_r(ctx, v - Infix_offset_hd(hd));
      break;
    case Custom_tag: {
      uintnat sz_32, sz_64;
      char * ident = Custom_ops_val(v)->identifier;
      void (*serialize)(value v, uintnat * wsize_32,
                        uintnat * wsize_64);
      //printf("[object at %p, which is a %s custom: BEGIN\n", (void*)v, Custom_ops_val(v)->identifier);
      if(extern_cross_context){
        //printf("About the object at %p, which is a %s custom: USING a cross-context serializer\n", (void*)v, Custom_ops_val(v)->identifier);
        serialize = Custom_ops_val(v)->cross_context_serialize;
      }
      else{
        //printf("About the object at %p, which is a %s custom: NOT using a cross-context serializer\n", (void*)v, Custom_ops_val(v)->identifier);
        serialize = Custom_ops_val(v)->serialize;
      }
      //printf("Still alive 100\n");
      if (serialize == NULL){
        //////
        //struct custom_operations *o = Custom_ops_val(v);
        //printf("About the object at %p, which is a %s custom\n", (void*)v, Custom_ops_val(v)->identifier); volatile int a = 1; a /= 0;
        ///////////
        extern_invalid_argument_r(ctx, "output_value: abstract value (Custom)");
      }
      //printf("Still alive 200\n");
      Write(CODE_CUSTOM);
      //printf("Still alive 300\n");
      writeblock_r(ctx, ident, strlen(ident) + 1);
      //printf("Still alive 400\n");
      serialize(v, &sz_32, &sz_64);
      //printf("Still alive 500\n");
      size_32 += 2 + ((sz_32 + 3) >> 2);  /* header + ops + data */
      size_64 += 2 + ((sz_64 + 7) >> 3);
      //printf("Still alive 600\n");
      extern_record_location_r(ctx,v); // This temporarily breaks the object, by replacing it with a forwarding pointer
      //printf("object at %p, which is a custom: END\n", (void*)v);
      break;
    }
    default: {
      value field0;
      if (tag < 16 && sz < 8) {
        Write(PREFIX_SMALL_BLOCK + tag + (sz << 4));
#ifdef ARCH_SIXTYFOUR
      } else if (hd >= ((uintnat)1 << 32)) {
        writecode64_r(ctx, CODE_BLOCK64, Whitehd_hd (hd));
#endif
      } else {
        writecode32_r(ctx, CODE_BLOCK32, Whitehd_hd (hd));
      }
      size_32 += 1 + sz;
      size_64 += 1 + sz;
      field0 = Field(v, 0);
      extern_record_location_r(ctx, v);
      /* Remember that we still have to serialize fields 1 ... sz - 1 */
      if (sz > 1) {
        sp++;
        if (sp >= extern_stack_limit) sp = extern_resize_stack_r(ctx, sp);
        sp->v = &Field(v,1);
        sp->count = sz-1;
      }
      /* Continue serialization with the first field */
      v = field0;
      continue;
    }
    }
  }
  else if ((cf = extern_find_code_r(ctx, (char *) v)) != NULL) {
    if (!extern_closures){
      extern_invalid_argument_r(ctx, "output_value: functional value"); // FIXME: this is the correct version. !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      //DUMP("output_value: functional value"); {volatile int a = 1; a /= 0;}
      }
    //fprintf(stderr, "ZZZZ dumping a code pointer: BEGIN\n");
    //DUMP("dumping a code pointer 0x%lx, or %li; code start is at %p", v, v, cf->code_start);
    writecode32_r(ctx, CODE_CODEPOINTER, (char *) v - cf->code_start);
    writeblock_r(ctx, (char *) cf->digest, 16);
    //dump_digest(cf->digest);
    //fprintf(stderr, "ZZZZ dumping a code pointer: END\n");
  } else {
    if(extern_cross_context){
      fprintf(stderr, "ZZZZ working on the external pointer: %p, which is to say %li [cf is %p]\n", (void*)v, (long)v, cf);
      //fprintf(stderr, "ZZZZ I'm doing a horrible, horrible thing: serializing the pointer as a tagged 0.\n");
      DUMP("about to crash in the strange case I'm debugging");
      /* DUMP("the object is 0x%lx, or %li ", v, v); */
      /* DUMP("probably crashing now"); */
      /* DUMP("tag is %i", (int)Tag_val(v)); */
      /* DUMP("size is %i", (int)Wosize_val(v)); */
      //volatile int a = 1; a /= 0;
      //extern_rec_r(ctx, Val_int(0));
      /* fprintf(stderr, "ZZZZ [This is probably wrong: I'm marshalling an out-of-heap pointer as an int64]\n"); */
      /* writecode64_r(ctx, CODE_INT64, (v << 1) | 1); */
      extern_invalid_argument_r(ctx, "output_value: abstract value (outside heap) [FIXME: implement]");
    }
    else
      extern_invalid_argument_r(ctx, "output_value: abstract value (outside heap)");
  }
  next_item:
    /* Pop one more item to marshal, if any */
    if (sp == extern_stack) {
        /* We are done.   Cleanup the stack and leave the function */
        extern_free_stack_r(ctx);
        return;
    }
    v = *((sp->v)++);
    if (--(sp->count) == 0) sp--;
  }
  /* Never reached as function leaves with return */
}

static intnat extern_value_r(CAML_R, value v, value flags)
{
  intnat res_len;
  int fl;
  /* Parse flag list */
  fl = caml_convert_flag_list(flags, extern_flags);
  extern_ignore_sharing = fl & NO_SHARING;
  extern_closures = fl & CLOSURES;
  extern_cross_context = fl & CROSS_CONTEXT;
  //printf("SETTING IGNORE SHARING: it's %s\n", (extern_ignore_sharing ? "YES" : "NO"));
  //printf("SETTING CLOSURES: it's %s\n", (extern_closures ? "YES" : "NO"));
  //printf("SETTING CROSS-CONTEXT: it's %s\n", (extern_cross_context ? "YES" : "NO"));
  /* Initializations */
  init_extern_trail_r(ctx);
  obj_counter = 0;
  size_32 = 0;
  size_64 = 0;
  /* Write magic number */
  write32_r(ctx, Intext_magic_number);
  /* Set aside space for the sizes */
  extern_ptr += 4*4;
  /* Marshal the object */
  extern_rec_r(ctx,v);
  /* Record end of output */
  close_extern_output_r(ctx);
  /* Undo the modifications done on externed blocks */
  extern_replay_trail_r(ctx);
  /* Write the sizes */
  res_len = extern_output_length_r(ctx);
#ifdef ARCH_SIXTYFOUR
  if (res_len >= ((intnat)1 << 32) ||
      size_32 >= ((intnat)1 << 32) || size_64 >= ((intnat)1 << 32)) {
    /* The object is so big its size cannot be written in the header.
       Besides, some of the array lengths or string lengths or shared offsets
       it contains may have overflowed the 32 bits used to write them. */
    free_extern_output_r(ctx);
    caml_failwith_r(ctx, "output_value: object too big");
  }
#endif
  if (extern_userprovided_output != NULL)
    extern_ptr = extern_userprovided_output + 4;
  else {
    extern_ptr = extern_output_first->data + 4;
    extern_limit = extern_output_first->data + SIZE_EXTERN_OUTPUT_BLOCK;
  }
  write32_r(ctx,res_len - 5*4);
  write32_r(ctx, obj_counter);
  write32_r(ctx, size_32);
  write32_r(ctx, size_64);
  return res_len;
}

void caml_output_val_r(CAML_R, struct channel *chan, value v, value flags)
{
  intnat len;
  struct output_block * blk, * nextblk;

  if (! caml_channel_binary_mode(chan))
    caml_failwith_r(ctx, "output_value: not a binary channel");
  init_extern_output_r(ctx);
  len = extern_value_r(ctx, v, flags);
  /* During [caml_really_putblock], concurrent [caml_output_val] operations
     can take place (via signal handlers or context switching in systhreads),
     and [extern_output_first] may change. So, save it in a local variable. */
  blk = extern_output_first;
  while (blk != NULL) {
    caml_really_putblock_r(ctx, chan, blk->data, blk->end - blk->data);
    nextblk = blk->next;
    free(blk);
    blk = nextblk;
  }
}

CAMLprim value caml_output_value_r(CAML_R, value vchan, value v, value flags)
{
  CAMLparam3 (vchan, v, flags);
  struct channel * channel = Channel(vchan);

  Lock(channel);
  caml_output_val_r(ctx, channel, v, flags);
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_output_value_to_string_r(CAML_R, value v, value flags)
{
  intnat len, ofs;
  value res;
  struct output_block * blk, * nextblk;

  init_extern_output_r(ctx);
  len = extern_value_r(ctx, v, flags);
  /* PR#4030: it is prudent to save extern_output_first before allocating
     the result, as in caml_output_val */
  blk = extern_output_first;
  res = caml_alloc_string_r(ctx, len);
  ofs = 0;
  while (blk != NULL) {
    int n = blk->end - blk->data;
    memmove(&Byte(res, ofs), blk->data, n);
    ofs += n;
    nextblk = blk->next;
    free(blk);
    blk = nextblk;
  }
  return res;
}

CAMLprim value caml_output_value_to_buffer_r(CAML_R, value buf, value ofs, value len,
                                           value v, value flags)
{
  intnat len_res;
  extern_userprovided_output = &Byte(buf, Long_val(ofs));
  extern_ptr = extern_userprovided_output;
  extern_limit = extern_userprovided_output + Long_val(len);
  len_res = extern_value_r(ctx, v, flags);
  return Val_long(len_res);
}

CAMLexport void caml_output_value_to_malloc_r(CAML_R, value v, value flags,
                                            /*out*/ char ** buf,
                                            /*out*/ intnat * len)
{
  intnat len_res;
  char * res;
  struct output_block * blk;

  init_extern_output_r(ctx);
  len_res = extern_value_r(ctx, v, flags);
  res = malloc(len_res);
  if (res == NULL) extern_out_of_memory_r(ctx);
  *buf = res;
  *len = len_res;
  for (blk = extern_output_first; blk != NULL; blk = blk->next) {
    int n = blk->end - blk->data;
    memmove(res, blk->data, n);
    res += n;
  }
  free_extern_output_r(ctx);
}

CAMLexport intnat caml_output_value_to_block_r(CAML_R, value v, value flags,
                                             char * buf, intnat len)
{
  intnat len_res;
  extern_userprovided_output = buf;
  extern_ptr = extern_userprovided_output;
  extern_limit = extern_userprovided_output + len;
  len_res = extern_value_r(ctx, v, flags);
  return len_res;
}

/* Functions for writing user-defined marshallers */

CAMLexport void caml_serialize_int_1_r(CAML_R, int i)
{
  if (extern_ptr + 1 > extern_limit) grow_extern_output_r(ctx, 1);
  extern_ptr[0] = i;
  extern_ptr += 1;
}

CAMLexport void caml_serialize_int_2_r(CAML_R, int i)
{
  if (extern_ptr + 2 > extern_limit) grow_extern_output_r(ctx, 2);
  extern_ptr[0] = i >> 8;
  extern_ptr[1] = i;
  extern_ptr += 2;
}

CAMLexport void caml_serialize_int_4_r(CAML_R, int32 i)
{
  if (extern_ptr + 4 > extern_limit) grow_extern_output_r(ctx, 4);
  extern_ptr[0] = i >> 24;
  extern_ptr[1] = i >> 16;
  extern_ptr[2] = i >> 8;
  extern_ptr[3] = i;
  extern_ptr += 4;
}

CAMLexport void caml_serialize_int_8_r(CAML_R, int64 i)
{
  caml_serialize_block_8_r(ctx, &i, 1);
}

CAMLexport void caml_serialize_float_4_r(CAML_R, float f)
{
  caml_serialize_block_4_r(ctx, &f, 1);
}

CAMLexport void caml_serialize_float_8_r(CAML_R, double f)
{
  caml_serialize_block_float_8_r(ctx, &f, 1);
}

CAMLexport void caml_serialize_block_1_r(CAML_R, void * data, intnat len)
{
  if (extern_ptr + len > extern_limit) grow_extern_output_r(ctx, len);
  memmove(extern_ptr, data, len);
  extern_ptr += len;
}

CAMLexport void caml_serialize_block_2_r(CAML_R, void * data, intnat len)
{
  if (extern_ptr + 2 * len > extern_limit) grow_extern_output_r(ctx, 2 * len);
#ifndef ARCH_BIG_ENDIAN
  {
    unsigned char * p;
    char * q;
    for (p = data, q = extern_ptr; len > 0; len--, p += 2, q += 2)
      Reverse_16(q, p);
    extern_ptr = q;
  }
#else
  memmove(extern_ptr, data, len * 2);
  extern_ptr += len * 2;
#endif
}

CAMLexport void caml_serialize_block_4_r(CAML_R, void * data, intnat len)
{
  if (extern_ptr + 4 * len > extern_limit) grow_extern_output_r(ctx,4 * len);
#ifndef ARCH_BIG_ENDIAN
  {
    unsigned char * p;
    char * q;
    for (p = data, q = extern_ptr; len > 0; len--, p += 4, q += 4)
      Reverse_32(q, p);
    extern_ptr = q;
  }
#else
  memmove(extern_ptr, data, len * 4);
  extern_ptr += len * 4;
#endif
}

CAMLexport void caml_serialize_block_8_r(CAML_R, void * data, intnat len)
{
  if (extern_ptr + 8 * len > extern_limit) grow_extern_output_r(ctx,8 * len);
#ifndef ARCH_BIG_ENDIAN
  {
    unsigned char * p;
    char * q;
    for (p = data, q = extern_ptr; len > 0; len--, p += 8, q += 8)
      Reverse_64(q, p);
    extern_ptr = q;
  }
#else
  memmove(extern_ptr, data, len * 8);
  extern_ptr += len * 8;
#endif
}

CAMLexport void caml_serialize_block_float_8_r(CAML_R, void * data, intnat len)
{
  if (extern_ptr + 8 * len > extern_limit) grow_extern_output_r(ctx,8 * len);
#if ARCH_FLOAT_ENDIANNESS == 0x01234567
  memmove(extern_ptr, data, len * 8);
  extern_ptr += len * 8;
#elif ARCH_FLOAT_ENDIANNESS == 0x76543210
  {
    unsigned char * p;
    char * q;
    for (p = data, q = extern_ptr; len > 0; len--, p += 8, q += 8)
      Reverse_64(q, p);
    extern_ptr = q;
  }
#else
  {
    unsigned char * p;
    char * q;
    for (p = data, q = extern_ptr; len > 0; len--, p += 8, q += 8)
      Permute_64(q, 0x01234567, p, ARCH_FLOAT_ENDIANNESS);
    extern_ptr = q;
  }
#endif
}

/* Find where a code pointer comes from */

static struct code_fragment * extern_find_code_r(CAML_R, char *addr)
{
  int i;
  for (i = caml_code_fragments_table.size - 1; i >= 0; i--) {
    struct code_fragment * cf = caml_code_fragments_table.contents[i];
    if (! cf->digest_computed) {
      caml_md5_block(cf->digest, cf->code_start, cf->code_end - cf->code_start);
      cf->digest_computed = 1;
    }
    if (cf->code_start <= addr && addr < cf->code_end) return cf;
  }
  return NULL;
}
