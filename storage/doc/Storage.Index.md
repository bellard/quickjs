# Index class

Index object in persistent storage.

## Properties

* ```index.length``` - integer, read-only, length of an index, number of objects associated represented by the index. 
* ```index.unique``` - boolean, read-only, true if the index was declared as unique. 
* ```index.type``` - string, read-only, key type as it was declared at creation time. Read-only property.

## Enumeration

Indexes support ```for(of)``` enumeration style:

```JavaScript:
// log all objects in the index 
for( var obj in index ) 
  console.log(obj);
```


## Methods

* ```index.set( key, obj [, replace: true|false ] ) : true|false```

  Inserts *obj* object into the index and associates it with the *key* value. Optionally, in-case of non-unique index, replaces it with existing object if such key is already present in the index.

* ```index.get( key ) returns: object | [objects...]```

  Returns object at the *key* position or null. *key* has to be of the same type as the type of the index object. If the index was created as non unique then the return value is an array - list of items under the key.

* ```index.delete( key [,obj] ) returns: true | false```

  Method removes object *obj* by key from the index. Method returns true on success, otherwise false. If the index is unique, obj is optional.

* ```index.select( minKey, maxKey [, ascending [, startInclusive [, endInclusive]]] ) returns: Iterator.```

  Returns selection in the Index based on criteria min-key, max-key, ascent or descent order, start-inclusive, end-inclusive. Default values: ```ascending:true```, ```startInclusive:true``` and ```endInclusive:true```.

  The method is intendeded to be used in ```for(of)``` enumerations:

   ```JavaScript:
   for( var obj in index.select(minVal, maxVal) ) { ... }
   ```

  Either minKey or maxKey can be *null* that means search from very first or very last key in the index.

* ```index.clear()``` 

  Removes all items from the index object - makes it empty.