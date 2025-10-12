// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>
#include <cstring>
#include <ivu_hash.hxx>
#include <ivu_string_hash.hxx>

class IVP_Hash_Elem {
 public:
  IVP_Hash_Elem *next;
  void *value;
  char key[1];
};

int IVP_Hash::hash_index(const char *key) const {
  unsigned int index = 0xffffffffU;
  for (int i = key_size - 1; i >= 0; i--) {
    const unsigned c = *((const unsigned char *)(key++));
    index = IVP_Hash_crctab[(index ^ c) & 0xff] ^ (index >> 8);
  }
  index = index % size;
  return index;
};

const unsigned int IVP_Hash_crctab[] = {
    0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U,
    0x706af48fU, 0xe963a535U, 0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U,
    0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U,
    0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
    0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U,
    0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU, 0x14015c4fU, 0x63066cd9U,
    0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
    0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
    0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U,
    0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU, 0x51de003aU,
    0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U,
    0xb8bda50fU, 0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
    0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU, 0x76dc4190U,
    0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
    0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU,
    0xe10e9818U, 0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
    0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU, 0x6c0695edU,
    0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U,
    0x8bbeb8eaU, 0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U,
    0xfbd44c65U, 0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
    0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
    0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U,
    0xaa0a4c5fU, 0xdd0d7cc9U, 0x5005713cU, 0x270241aaU, 0xbe0b1010U,
    0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
    0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U,
    0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U,
    0x03b6e20cU, 0x74b1d29aU, 0xead54739U, 0x9dd277afU, 0x04db2615U,
    0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
    0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U,
    0x8708a3d2U, 0x1e01f268U, 0x6906c2feU, 0xf762575dU, 0x806567cbU,
    0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U, 0x10da7a5aU,
    0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
    0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U,
    0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU, 0xd80d2bdaU, 0xaf0a1b4cU,
    0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
    0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
    0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU, 0xc5ba3bbeU,
    0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U, 0xb5d0cf31U,
    0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU,
    0x026d930aU, 0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
    0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U, 0x92d28e9bU,
    0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
    0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U,
    0x18b74777U, 0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
    0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U, 0xa00ae278U,
    0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U,
    0x4969474dU, 0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U,
    0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
    0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
    0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U,
    0x5d681b02U, 0x2a6f2b94U, 0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU,
    0x2d02ef8dU, 0x8350e449U, 0xe2438426U, 0xe5338a1bU, 0xc460be49U,
    0xe77abe1aU, 0x8b40b91bU, 0xde7da649U, 0xec719f49U, 0x9a2af45cU,
    0x862af447U, 0x8b56bb08U, 0xc770a219U, 0xd2339b59U, 0x852af447U,
    0x8b47a500U, 0xd833b90cU, 0xd367ed1eU, 0xca60ed0cU, 0xc570bf10U,
    0xdb67a80dU, 0x8b64a41dU, 0xc3338826U, 0xf933fd11U, 0xea51fc5aU,
    0xe857fb50U, 0x852da8a9U, 0x5bc46db8U, 0x304ef5ceU, 0x19beb02bU,
    0xd31fabc6U, 0x51c967dbU, 0x8a6559acU, 0x5caf5e87U, 0x50eb6284U,
    0x7a437207U, 0x05ebdb0bU, 0x0d806876U, 0x0b1f02f2U, 0x4d698260U,
    0x5bf00380U, 0x0a7115f7U, 0xf09b4147U, 0x725e51e0U, 0xeda02684U,
    0xe381c380U, 0x35ce48efU, 0xf3c74223U, 0x124df932U, 0x6a1d3474U,
    0x3d948118U, 0x7ffb3775U, 0x36f5c78cU, 0xd20b0270U, 0x5e455226U,
    0x873c578fU, 0x25dd8f39U, 0x772ea31dU, 0x6fa928f3U, 0x0ff37b2aU,
    0x63d3e42aU, 0x72ea90b9U, 0xfaefed17U, 0x86a9a8d2U, 0x21bbc001U,
    0x4d6810c5U, 0x9cd08ab5U, 0x0eeeed06U, 0x14597ff0U, 0xee02a7ffU,
    0xa498db55U, 0xec6f35bbU, 0x8d15f664U, 0x290a74d3U, 0x76115aeaU,
    0xaaa43ea9U, 0xa9b4962eU, 0x13d513aeU, 0xf9e6c9f3U, 0xd9eea0f0U,
    0xaebb6beeU, 0x69728ec9U, 0xcfc9e80cU, 0x390aebfcU, 0x6b911013U,
    0xe2ae8ff5U, 0x1854fa4eU, 0xeeb70d22U, 0xe57b1e7bU, 0x62047f46U,
    0x5acb59ecU, 0x5aa8d6daU, 0x95ebe3eeU, 0x1d62e802U, 0x134f5f97U,
    0xa9408739U, 0x100dafefU, 0x9e05784eU, 0x45586188U, 0xacdb5b2fU,
    0x6ad60310U, 0x9ce02986U, 0xfb6b223bU, 0x60296ee1U, 0xf314dde7U,
    0xb5d57b64U, 0xb9772273U, 0x8694ac86U, 0xe623a7ecU, 0x8f5cbb1cU,
    0x637505fdU, 0x0b7ed390U, 0xdf3c45fbU, 0x5e536d60U, 0x0f82e9bbU,
    0xb2960f0eU, 0x1e3d21b3U, 0xd5709c2eU, 0x62091602U, 0x2ce9a895U,
    0x81fd5d48U, 0x8422e6baU, 0x695c6bb5U, 0x7f7988e2U, 0x3321aa7eU,
    0x3dab9308U, 0x79280132U, 0x9c209408U, 0x89fa4e38U, 0xb0bdebb7U,
    0x56de71baU, 0x9211adb8U, 0x9ed424a5U, 0x2a5ae23cU, 0x2eef7dd5U,
    0x9b18d23aU, 0x5fb16878U, 0xf9d82239U, 0x31bee8baU, 0x1b966b8cU,
    0xe5837eb9U, 0x71f1eb08U, 0x74c4d378U, 0x492da9beU, 0x4a18255dU,
    0x7cca491bU, 0x2cb98b51U, 0x9afed2e9U, 0xd4c09c33U, 0x1d6476feU,
    0xe4c9655aU, 0xb21dc693U, 0xb2e7fd99U, 0x65a7a93eU, 0xcbb85df8U,
    0x32a3de70U, 0x2f77be52U, 0x77231e45U, 0xa06ead4cU, 0x207fecccU,
    0xca425a58U, 0xace8450eU, 0x0309342bU, 0x611e9626U, 0xd9dc4626U,
    0xab4cb165U, 0x77cfff00U, 0xe240ddc3U, 0x0fbb11c5U, 0x0f76d827U,
    0x2b1c5d22U, 0xb6f856e6U, 0x173413d4U, 0x2c48040cU, 0x92e802ebU,
    0xfbcce39cU, 0x23186f47U, 0xdaabb198U, 0x631df38cU, 0x51cabbdeU,
    0xaa46b8e0U, 0x1a532382U, 0x96ada3e7U, 0x348e4940U, 0x033b9a72U,
    0x4bc7c0c0U, 0x49585db4U, 0x8e145b4eU, 0xf61e7d79U, 0xe6b336e2U,
    0xcb78d769U, 0xa28de4dbU, 0x6c9200c1U, 0xfcc99e53U, 0x8cf0d424U,
    0x40a3b820U, 0x16359465U, 0x6c455a41U, 0x6aa2e4a2U, 0xfa40b370U,
    0x7f5e0f45U, 0x8204aa39U, 0x5192508fU, 0x9907fc99U, 0x9098316aU,
    0x4a86e1cdU, 0xe346bcf3U, 0x03fc7917U, 0x9664670fU, 0x24007ae3U,
    0x3f45bfaeU, 0xc1b774ccU, 0x9ba5657aU, 0xe6c675fcU, 0x8039fdbdU,
    0xb1f79e31U, 0xf6ee7285U, 0xb964b5cfU, 0x66f8a250U, 0x3b3a13a9U,
    0x4b9b1947U, 0xf59e0ee0U, 0x1ce792b8U, 0x71a1e650U, 0x1bf9ebabU,
    0xc98ca758U, 0x20c9a775U, 0xaf59108dU, 0x79a1d95bU, 0xebcb719eU,
    0x650f07c0U, 0x65e62fe9U, 0x4a19892cU, 0x01bdbb5fU, 0x21f29ee7U,
    0x8622b969U, 0x489afe4cU, 0xc9fcd058U, 0xa7fb16b2U, 0x75ac91a9U,
    0x62b3ca1cU, 0xfb6e66b3U, 0xcbeda7e4U, 0x9acc437fU, 0xc2d2f6a5U,
    0x194a30d6U, 0xe9c9564bU, 0x31e42e0cU, 0x32f91783U, 0xc29508a3U,
    0x2d2295d1U, 0xbaf50313U, 0x02188a35U, 0xce55d1c1U, 0x8aab06d5U,
    0x1aa3ef22U, 0x30effb6fU};

IVP_Hash::IVP_Hash(int sizei, int key_sizei, void *not_found_valuei) {
  size = sizei;
  key_size = key_sizei;
  not_found_value = not_found_valuei;
  elems = (IVP_Hash_Elem **)p_calloc(sizeof(void *), size);
}

IVP_Hash::~IVP_Hash() {
  int i;
  for (i = 0; i < size; i++) {
    if (!elems[i]) continue;
    IVP_Hash_Elem *next, *el;
    for (el = elems[i]; el; el = next) {
      next = el->next;
      P_FREE(el);
    }
  }
  P_FREE(elems);
}

void *IVP_Hash::find(const char *key) const {
  int i = hash_index(key);
  IVP_ASSERT(i >= 0);
  IVP_Hash_Elem *el;
  for (el = elems[i]; el; el = el->next) {
    if (!memcmp(el->key, key, key_size)) break;
  }
  if (el) {
    return el->value;
  } else {
    return not_found_value;
  }
}

void IVP_Hash::add(const char *key, void *val) {
  int i = hash_index(key);

  //    ivp_message("hash index: '%d', key: %lx key_string: '%s'\n", i, key,
  //    key);

  IVP_Hash_Elem *el =
      (IVP_Hash_Elem *)p_malloc(sizeof(IVP_Hash_Elem) + key_size);  //-1
  memcpy(el->key, key, key_size);
  el->next = elems[i];
  elems[i] = el;
  el->value = val;
}

void IVP_Hash::remove(const char *key) {
  int i = hash_index(key);
  IVP_Hash_Elem *el, *last_el;
  last_el = 0;
  for (el = elems[i]; el; el = el->next) {
    if (!memcmp(el->key, key, key_size)) {
      if (last_el) {
        last_el->next = el->next;
      } else {
        elems[i] = el->next;
      }
      el->next = 0;
      P_FREE(el);
      return;
    }
    last_el = el;
  }
}

int IVP_U_String_Hash::hash_index(const char *key) const {
  unsigned int index = 0xffffffffU;
  const hk_intp key_size = strlen(key);
  for (hk_intp i = key_size - 1; i >= 0; i--) {
    const unsigned int c = *((const unsigned char *)(key++));
    index = IVP_Hash_crctab[(index ^ c) & 0xff] ^ (index >> 8);
  }
  index = index % size;
  return index;
};

IVP_U_String_Hash::IVP_U_String_Hash(int sizei, void *not_found_valuei) {
  size = sizei;
  not_found_value = not_found_valuei;
  elems = (IVP_Hash_Elem **)p_calloc(sizeof(void *), size);
}

IVP_U_String_Hash::~IVP_U_String_Hash() {
  int i;
  for (i = 0; i < size; i++) {
    if (!elems[i]) continue;
    IVP_Hash_Elem *next, *el;
    for (el = elems[i]; el; el = next) {
      next = el->next;
      P_FREE(el);
    }
  }
  P_FREE(elems);
}

void *IVP_U_String_Hash::find(const char *key) const {
  int i = hash_index(key);
  IVP_ASSERT(i >= 0);
  IVP_Hash_Elem *el;
  for (el = elems[i]; el; el = el->next) {
    if (!strcmp(&el->key[0], key)) break;
  }
  if (el) {
    return el->value;
  } else {
    return not_found_value;
  }
}

void IVP_U_String_Hash::add(const char *key, void *val) {
  int i = hash_index(key);
  hk_intp keysize = strlen(key);
  IVP_Hash_Elem *el =
      (IVP_Hash_Elem *)p_malloc(sizeof(IVP_Hash_Elem) + keysize);
  memcpy(el->key, key, keysize + 1);
  el->next = elems[i];
  elems[i] = el;
  el->value = val;
}

void IVP_U_String_Hash::remove(const char *key) {
  int i = hash_index(key);
  IVP_Hash_Elem *el, *last_el;
  last_el = 0;
  for (el = elems[i]; el; el = el->next) {
    if (!strcmp(&el->key[0], key)) {
      if (last_el) {
        last_el->next = el->next;
      } else {
        elems[i] = el->next;
      }
      el->next = 0;
      P_FREE(el);
      return;
    }
    last_el = el;
  }
}
