#ifndef __HASHTAB_H__
#define __HASHTAB_H__

static const length_t dbHashtableSize = 1013;

// if it returns non-zero - stops enumeration
typedef int each_cb(void* key, unsigned int key_length, void* data, void* opaque);

class dbHashtable {
private:
  struct Entry {
    Entry *  next;
    void *   value;
    void *   key;
    length_t keySize;
    unsigned hashCode;
  };

  Entry **table;

  static unsigned calculateHashCode(void *key, length_t keySize) {
    unsigned char *p = (unsigned char *)key;
    int            n = int(keySize);
    unsigned       h = 0;
    while (--n >= 0) {
      h = (h << 2) ^ *p++;
    }
    return h;
  }

  dbHashtable(const dbHashtable &);
  dbHashtable &operator=(const dbHashtable &);

public:
  dbHashtable() {
    table = new Entry *[dbHashtableSize];
    memset(table, 0, sizeof(Entry *) * dbHashtableSize);
  }

  void put(void *key, length_t keySize, void *value) {
    unsigned hashCode = calculateHashCode(key, keySize);
    Entry *  e        = new Entry();
    e->hashCode       = hashCode;
    e->key            = key;
    e->keySize        = keySize;
    e->value          = value;
    unsigned h        = hashCode % dbHashtableSize;
    e->next           = table[h];
    table[h]          = e;
  }

  void *get(void *key, length_t keySize) {
    unsigned hashCode = calculateHashCode(key, keySize);
    unsigned h        = hashCode % dbHashtableSize;
    for (Entry *e = table[h]; e != NULL; e = e->next) {
      if (e->hashCode == hashCode && e->keySize == keySize &&
          memcmp(e->key, key, keySize) == 0) {
        return e->value;
      }
    }
    return NULL;
  }

  void *remove(void *key, length_t keySize) {
    Entry ** epp, *ep;
    unsigned hashCode = calculateHashCode(key, keySize);
    unsigned h        = hashCode % dbHashtableSize;
    for (epp = &table[h]; (ep = *epp) != NULL; epp = &ep->next) {
      if (ep->hashCode == hashCode && memcmp(ep->key, key, keySize) == 0) {
        *epp = ep->next;
        return ep->value;
      }
    }
    return NULL;
  }

  void clear() {
    for (int i = dbHashtableSize; --i >= 0;) {
      Entry *e, *next;
      for (e = table[i]; e != NULL; e = next) {
        next = e->next;
        delete e;
      }
      table[i] = NULL;
    }
  }

  void each(each_cb* pcb, void* opaque) {
    for (int i = dbHashtableSize; --i >= 0;) {
      Entry *e, *next;
      for (e = table[i]; e != NULL; e = next) {
        next = e->next;
        if (pcb(e->key, e->keySize, e->value, opaque))
          break;
      }
    }
  }


  ~dbHashtable() {
    clear();
    delete[] table;
  }
};

#endif
