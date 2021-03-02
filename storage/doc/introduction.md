## Introduction

This module provides built-in data persistence - data storage and retrieval.

“Built-in” here means that there is no special entity like Database Access Client or Database Driver in the language to access persistent data – it is rather that objects in storage (database file) are ordinary script entities: objects (key/value maps), arrays (lists of values), and primitive types (string, integer, long (a.k.a. BigInt), float, date, byte-vectors (ArrayBuffer), boolean and null).

You can think of QuickJS+persistence as a script with built-in NoSQL database mechanism where access to data in DB is provided by standard language means: get/set properties of stored objects or get/set elements of stored arrays and indexes.

To support persistence this module introduces two helper classes: Storage and Index.

## Storage

Storage object represents database file in script. It is used as to create databases as to open existing databases to get access to data stored in them.

To create or open existing database you will use ```Storage.open(path)``` method. On success this method returns instance of Storage class that has few properties and methods, the most interesting of them is ```storage.root``` property:

### ```storage.root```

```storage.root``` property is a reference to stored root object (or array).

> **All objects accessible from (contained in) the ```storage.root``` object are automatically persistent – stored in the DB.**

As simple as that. ```storage.root``` is ordinary script object that can be used by standard script means to access and/or modify data in storage.

When you will make any change in any object or collection under that root object it will be stored to the database (a.k.a. persisted) without need to send that data anywhere or call any method explicitly.

#### Example #1, storage opening and its structure initialization:

Idiomatic code to open or create database looks like this:

```JavaScript
import * as Storage from "storage"; // or "@storage" if Sciter.JS

var storage = Storage.open("path/to/data/file.db");
var root = storage.root || initDb(storage); // get root data object or initialize DB
```

where ```initDb(storage)``` is called only when storage was just created and so is empty – its root is null in this case. That function may look like as:

```JavaScript
function initDb(storage) {
  storage.root = { 
     version: 1, // integer property ("integer field" in DB terms)
     meta: {}, // sub-object
     children: [] // sub-array, empty initially
  }; 
  return storage.root;
}
```


#### Example #2, accessing and populating persistent data:

Having the ```root``` variable containing persistent root object, we can access and populate the data in it as we normally do – no other special mechanism is required:

```JavaScript
// printout elements of root.children collection (array)

  let root = storage.root;
  for( let child of root.children ) 
    console.log(child.name, child.nickname);
}
```

In the same way, to populate the data in storage we use standard JavaScript means:

```JavaScript
var collection = root.children; // plain JS array
  collection.push( { name: "Mikky", age: 7 } ); // calling Array's method push() to add 
  collection.push( { name: "Olly", age: 6 } );  // objects to the collection
  collection.push( { name: "Linus", age: 5 } );
} 
```

Nothing special as you see – the code is not anyhow different from ordinary script code accessing and populating any data in script heap.

## Index

By default JavaScript supports collections of these built-in types:

* objects – unordered name/value maps and
* arrays – ordered lists of values with access by index (some integer).
* auxiliary collections - [Weak]Map and [Weak]Set.

These collections allow to organize data in various ways but sometimes these are not enough. We may need something in between of them – collections that are a) ordered but b) allow to access elements by keys at the same time. Example: we may need collection of objects that is ordered by their time of creation so we can present the collection to the user in data “freshness” order.

To support such use cases the module introduces Index objects.

> **Index is a keyed persistent collection that can be assigned to properties of other persistent objects or placed into arrays. Indexes provide effective access and ordering of potentially large data sets.**

Indexes support string, integer, long (BigInt), float and date keys and contain objects as index elements (a.k.a. records).

### Defining and populating indexes

Indexes are created by ```storage.createIndex(type[,unique]) : Index``` method, where

* *type* defines type of keys in the index. It can be "string", "integer", "long", "float" or "date".
* *unique* is either
  * *true* if the index support only unique keys, or
  * *false* if records with the same key values are allowed in the index.

#### Example #3: creating indexes, simple storage of notes:

To open storage database we can reuse code above, but storage initialization routine will look different this time:

```JavaScript
function initNotesDb(storage) { 
  storage.root = { 
    version: 1, 
    notesByDate: storage.createIndex("date",false), // list of notes indexed by date of creation
    notesById:   storage.createIndex("string",true) // list of notes indexed by their UID
  }
  return storage.root; 
}
```

As you see the storage contains two indexes: one will list notes by their date of creation and other will contain the same notes but ordered by unique ID.

Having such setup, adding notes to DB is trivial:

```JavaScript
function addNote(storage, noteText) {
  var note = {
    id   : UUID.create(), // generate UID  
    date : new Date(),
    text : noteText  
  };
  storage.root.notesByDate.set(note.date, note); // adding the note
  storage.root.notesById.set(note.id, note);     // to indexes
  return note; // returns constructed note object to the caller
}
```

We use here ```index.set(key,value)``` method to add items.

### Index selection – traversal and retrieval of index items

Getting elements of unique indexes is trivial – we use ```index.get(key)``` method similar to methods of standard Map or Set collections:

```JavaScript
function getNoteById(noteId) {
  return storage.root.notesById.get(noteId); // returns the note or undefined 
}
```

To get items from non-unique indexes we need pair of keys to get items in range between keys by using ```index.select()``` method:

```JavaScript
function getTodayNotes() {
  let now = new Date();
  let yesterday = new Date(now.year,now.month,now.day-1);
  var notes = [];
  for(let note of storage.root.select(yesterday,now)) // get range of notes from the index
    notes.push(note);
  return notes;
}
```

## Persistence of objects of custom classes

So far we were dealing with plain objects and arrays, but the Storage allows to store objects of custom classes too. This can be useful if your data objects have specific methods. Let’s refactor our notes storage to use it in OOP way:


```JavaScript
// module NotesDB.js 

import * as Storage from "storage"; // or "@storage" if Sciter.JS

const storage = ... open DB and optionally initialize the DB ...

class Note {
  
  constructor(text, date = undefined, id = undefined) {
    this.id = id || UUID.create();
    this.date = date || new Date();
    this.text = text;
    
    // adding it to storage
    let root = storage.root;
    root.notesByDate.set(this.date, this); 
    root.notesById.set(this.id, this);
  }

  remove() {
    let root = storage.root;
    root.notesByDate.delete(this.date, this); // need 'this' here as index is not unique
    root.notesById.delete(this.id);
  }

  static getById(id) {
    return storage.root.notesById.get(id); // will fetch object from DB and do 
                                           // Object.setPrototypeOf(note,Note.prototype)
  }
}
```

Technical details: while storing objects of customs classes, the Storage will store just name of object’s class. Database cannot contain neither classes themselves nor any functions – just pure data. On loading objects of custom classes, the runtime will try to bind classes from current scopes with instances of objects by updating prototype field of such objects.

## As an afterword

[Sciter Notes](https://notes.sciter.com/) application is a practical example of Storage use. 

That particular application uses Sciter/TIScript but principles are the same. You can see it’s [database handling routines on GitHub](https://github.com/c-smile/sciter-sdk/tree/master/notes/res/db), in particular its DB initialization may look in JS as:

```JavaScript
//|
//| open database and initialze it if needed
//|

function openDatabase(pathname)
{
  //const DBNAME = "sciter-notes.db";
  //const pathname = dbPathFromArgs() || ...;
  var ndb = Storage.open(pathname);
  if(!ndb.root) { 
    // new db, initialize structure
    ndb.root = 
      {
        id2item   :ndb.createIndex("string", true), // main index, item.id -> item, unique
        date2item :ndb.createIndex("date", false),  // item by date of creation index, item.cdate -> item, not unique
        tags      :{},  // map of tagid -> tag
        books     :{},  // map of bookid -> book; 
        version   :1,
      };
  }
  ndb.path = pathname;
  return ndb;
}
```

