// Function to recursively iterate through built-in names.
function collectBuiltinNames(obj, visited = new Set(), result = new Set()) {
  // Check if the object has already been visited to avoid infinite recursion.
  if (visited.has(obj))
    return;

  // Add the current object to the set of visited objects
  visited.add(obj);
  // Get the property names of the current object
  const properties = Object.getOwnPropertyNames(obj);
  // Iterate through each property
  for (var i=0; i < properties.length; i++) {
    var property = properties[i];
    if (property != "collectBuiltinNames" && typeof property != "number")
      result.add(property);
    // Check if the property is an object and if so, recursively iterate through its properties.
    if (typeof obj[property] === 'object' && obj[property] !== null)
      collectBuiltinNames(obj[property], visited, result);
  }
  return result;
}

// Start the recursive iteration with the global object.
console.log(Array.from(collectBuiltinNames(this)).join('\n'));
