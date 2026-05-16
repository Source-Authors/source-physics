// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _IVP_U_MINHASH_INCLUDED
#define _IVP_U_MINHASH_INCLUDED

// #ifdef DEBUG
#define SORT_MINDIST_ELEMENTS
// #endif

// dimhotepus: class -> struct
struct IVP_U_Min_Hash_Elem;

// dimhotepus: class -> struct
struct IVP_U_Min_Hash_Elem {
  IVP_U_Min_Hash_Elem *next;
  IVP_DOUBLE value;
#if defined(SORT_MINDIST_ELEMENTS)
  int cmp_index;
#endif
  void *elem;
};

extern const unsigned int IVP_Hash_crctab[];

class IVP_U_Min_Hash {
  friend class IVP_U_Min_Hash_Enumerator;
  hk_intp hash_index(const hk_intp *key) const {
    hk_uintp x = *key * 101;
    hk_uintp y = (x >> 8) + *key * 1001;
    return y & (size - 1);
  }
  // dimhotepus: Reordered members to reduce size on x86-64.
  IVP_U_Min_Hash_Elem **stadel;
  IVP_U_Min_Hash_Elem **min_per_array_pos;
  IVP_U_Min_Hash_Elem **elems;
  unsigned int size;
  void min_added_at_index(IVP_U_Min_Hash_Elem *elem, hk_intp i);
  void min_removed_at_index(IVP_U_Min_Hash_Elem *elem, hk_intp i);

 public:
  int counter;

  IVP_U_Min_Hash(int size = 256);  // size must be 2^x
  ~IVP_U_Min_Hash();

  void add(void *elem, IVP_DOUBLE value);
  void change_value(void *elem, IVP_DOUBLE new_value);

  void *find_min_elem() {
    if (!stadel[1])
      return NULL;
    else
      return stadel[1]->elem;
  }
  IVP_DOUBLE find_min_value() const { return stadel[1]->value; }
  bool is_elem(void *elem) const;
  void remove(void *elem);
  void remove_min();
};

class IVP_U_Min_Hash_Enumerator {
  IVP_U_Min_Hash *min_hash;
  IVP_U_Min_Hash_Elem *loop_elem;
  int loop_index;

 public:
  IVP_U_Min_Hash_Enumerator(IVP_U_Min_Hash *mh) {
    min_hash = mh;
    loop_elem = 0;
    loop_index = -1;
  }

  void *get_next_element() {
    if (loop_elem) {
      loop_elem = loop_elem->next;
    }
    while (!loop_elem) {
      loop_index++;
      if (loop_index >= (int)min_hash->size) return NULL;
      loop_elem = min_hash->elems[loop_index];
    }
    return loop_elem->elem;
  }

  void *get_next_element_lt(IVP_DOUBLE max_limit) {
    while (1) {
      if (loop_elem) {
        loop_elem = loop_elem->next;
      }
      while (!loop_elem) {
        loop_index++;
        if (loop_index >= (int)min_hash->size) return NULL;
        loop_elem = min_hash->elems[loop_index];
      }
      if (loop_elem->value < max_limit) {
        return loop_elem->elem;
      }
    }
  }
};

#endif
