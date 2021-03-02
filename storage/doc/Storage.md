
# Storage class

Represents persistent storage.

## Properties

* ```root``` - object, root object in the storage. Read/write property.

## Methods


* ```Storage.open(filename : string [,allowWrite: true] ) : storage | null```

  Static method. Opens the storage and returns an instance of Storage object. If *allowWrite* is *false* then storage is opened in read-only mode. 

* ```storage.close()```

  Closes underlying Storage object. Commits all data before cloasing. After closing the storage all persistent objects that are still in use are set to non-persistent state.

* ```storage.commit()```

  Commits (writes) all persistent objects reachable from its root into storage.

* ```storage.createIndex(type : string [, unique: bool]) returns: Index | null```

  Creates an index of given type and returns the index object. Index can have unique or duplicated keys depending on unique argument. Default value for *unique* is *true*. Supported types: "integer", "long", "float", "date" and "string".
