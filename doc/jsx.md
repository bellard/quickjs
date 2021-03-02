# JSX - JavaScript XML/HTML language extensions

Basic idea, this JS+X expression:

```javascript
var data = <div id="foo">Hello world!</div>;
```
Gets parsed into code equivalent to this:

```javascript
var data = JSX("div", {id:"foo"}, ["Hello world"]]);
```
Where JSX is a global function, so called "JSX driver", defined elsewhere with the following signature:

```javascript
function JSX(tag,atts,kids) {...}
```
where:

* `tag` - string, tag name: "div","p", ...;
* `atts` - object, element attributes: `{id:"foo"}`
* `kids` - array, child sub elements - strings or results of sub-calls of JSX.

# Use cases

JSX can be used with popular JS UI libraries as it is.

## [Mithril](https://mithril.js.org) 

This canonic Mithril sample:
```javascript
var count = 0 // added a variable

var Hello = {
    view: function() {
        return m("main", [
            m("h1", {class: "title"}, "My first app"),
            // changed the next line
            m("button", {onclick: function() {count++}}, count + " clicks"),
        ])
    }
}

m.mount(root, Hello)
```

Can be rewritten using JS+X in more natural way as:

```javascript

JSX = m; // using m() mithril function "as it is" as JSX driver!

var count = 0 // added a variable

var Hello = {
    view: function() {
        return <main>
          <h1 class="title">My first app</h1>
          <button onclick={function() {count++}}> {count} clicks</button>
        </main>
    }
}

m.mount(root, Hello)
```

## [ReactJS](https://reactjs.org/) 

ReactJS shall also be able to use JSX as it is, it is just a matter of declaring

```javascript

JSX = React.createElement; // using ReactJS VNode creator as JSX driver
```

# JSX syntax details

1. Tags and attribute names shall follow JS/XML naming convention and may contain `-` (dashes) inside.
2. Attributes and element content may contain expressions enclosed into `{` and `}` brackets:

```javascript
var className = "foo";
var listItems = ["one","two", "three"];

function renderList() {
  return <ul class={className}>
    { listItems.map( item => <li>{item}</li> ) }
  </ul>
}
```
to generate list:
```html
<ul>
  <li>one</li>
  <li>two</li>
  <li>three</li>
</ul>
```  

# To enable JSX support

To enable JSX support on QuickJS from this repository define CONFIG_JSX option while compiling.

[premake5.lua](https://github.com/c-smile/quickjspp/blob/master/premake5.lua#L23) script already defines CONFIG_JSX.

