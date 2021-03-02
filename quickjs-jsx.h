
/* JSX translates XML literals like this
 *
 *   var jsx = <div foo="bar">some text</div>;
 *
 * into calls of "JSX driver function":
 *
 *  __jsx__("div", {foo:"bar"}, ["some text"]);
 *
 * note: 
 *  a) the call always have 3 arguments: string, object|null, array|null 
 *  b) __jsx__ can be redefined, e.g. for https://mithril.js.org it will be just
 *     
 *     __jsx__ = m; // using mithril as JSX driver 
 */

static __exception int next_web_token(JSParseState *s) {
  s->allow_web_name_token = 1;
  int r = next_token(s);
  s->allow_web_name_token = 0;
  return r;
}

static int js_parse_jsx_expr(JSParseState *s, int level)
{
  int atts_count = 0;
  int kids_count = 0;

  // NOTE: caller already consumed '<' 
  if (next_web_token(s)) goto fail;
  if (s->token.val != TOK_IDENT)
    return js_parse_error(s, "Expecting tag name");

  //tag
  JSAtom  tag_atom = s->token.u.ident.atom;
  JSValue tag = JS_AtomToString(s->ctx,tag_atom);

  // load JSX function - driver of JSX expressions:
#if 1 // load it as a global function
  emit_op(s, OP_get_var);
  emit_atom(s, JS_ATOM_JSX);
#else // load it as a local/scope function - do we need that?
  emit_op(s, OP_scope_get_var);
  emit_atom(s, JS_ATOM_JSX);
  emit_u16(s, s->cur_func->scope_level);
#endif

  //      #0   #1   #2
  // JSX(tag, atts ,kids); where
  //  - atts - object {...}, can be empty
  //  - kids - array [...], can be empty

  char buf[ATOM_GET_STR_BUF_SIZE];
  const char* tag_chars = JS_AtomGetStr(s->ctx, buf, countof(buf), tag_atom);
  
  // check for tag name starting from capital letter 
  uint32_t res[3] = {0};
  lre_case_conv(res, tag_chars[0], 1);

  if (res[0] != tag_chars[0]) { // tag name starts from upper case.
    // ReactJS convention, if tag started from capital letter - it is either class or function
    // Fetch it here, so the tag in JSX call can be reference to class(constructor) or a function
    emit_op(s, OP_scope_get_var);
    emit_atom(s, tag_atom);
    emit_u16(s, s->cur_func->scope_level);
  } else 
  emit_push_const(s, tag, 0);
  
  JS_FreeValue(s->ctx, tag);

  // parse attributes
  JSAtom  attr_name = JS_ATOM_NULL;
  JSValue attr_value = JS_UNINITIALIZED;

#if defined(CONFIG_JSX_SCITER) // HTML shortcuts used by Sciter
  char class_buffer[512] = {0};
#endif

  if (next_web_token(s)) goto fail;
  
  emit_op(s, OP_object);

  while (s->token.val != '>') {
    
    if (s->token.val == '/') {
      next_token(s);
      //json_parse_expect(s, '>');
      if(s->token.val != '>')
        js_parse_error(s, "expecting '>'");
      goto GENERATE_KIDS;
    }
#if defined(CONFIG_JSX_SCITER) // HTML shortcuts used by Sciter
    if (s->token.val == '#') { // <div #some> ->  <div id="some">
      if (next_web_token(s)) goto fail;
      if (s->token.val != TOK_IDENT) {  js_parse_error(s, "expecting identifier"); goto fail; }
      attr_name = JS_NewAtom(s->ctx,"id");
      attr_value = JS_AtomToString(s->ctx, s->token.u.ident.atom);
      goto PUSH_ATTR_VALUE;
    }
    if (s->token.val == '|') { // <input|text> ->  <input type="text">
      if (next_web_token(s)) goto fail;
      if (s->token.val != TOK_IDENT) { js_parse_error(s, "expecting identifier"); goto fail; }
      attr_name = JS_NewAtom(s->ctx, "type");
      attr_value = JS_AtomToString(s->ctx, s->token.u.ident.atom);
      goto PUSH_ATTR_VALUE;
    }
    if (s->token.val == '(') { // <input(foo)> ->  <input name="foo">
      if (next_web_token(s)) goto fail;
      if (s->token.val != TOK_IDENT) { js_parse_error(s, "expecting identifier"); goto fail; }
      attr_name = JS_NewAtom(s->ctx, "name");
      attr_value = JS_AtomToString(s->ctx, s->token.u.ident.atom);
      if (next_token(s)) goto fail;
      if (s->token.val != ')') { js_parse_error(s, "expecting ')'"); goto fail; }
      goto PUSH_ATTR_VALUE;
    }
    if (s->token.val == '.') { // <div.some> ->  <div class="some">
      if (next_web_token(s)) goto fail;
      if (s->token.val != TOK_IDENT) { js_parse_error(s, "expecting identifier"); goto fail; }
      char cls1[256];
      const char *name = JS_AtomGetStr(s->ctx, cls1, countof(cls1), s->token.u.ident.atom);
      if (strlen(class_buffer) + strlen(name) + 2 < countof(class_buffer)) {
        if(class_buffer[0]) strcat(class_buffer, " ");
        strcat(class_buffer, name);
      }
      next_web_token(s);
      continue;
    }
#endif
    if (token_is_ident(s->token.val)) {
      /* keywords and reserved words have a valid atom */
      attr_name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
      if (next_token(s))
        goto fail;
    }

    if (js_parse_expect(s, '='))
      goto fail;
      
    if (s->token.val == TOK_STRING) {
      attr_value = JS_DupValue(s->ctx,s->token.u.str.str);
 PUSH_ATTR_VALUE:
      if (emit_push_const(s, attr_value, 0))
        goto fail;
      JS_FreeValue(s->ctx, attr_value);
    }
    else if (s->token.val == '{')
    {
      if (next_token(s))
        goto fail;
      if (js_parse_assign_expr(s /*, TRUE*/))
        goto fail;
      if(s->token.val != '}')
        return js_parse_error(s, "expecting '}'");
    }

    set_object_name(s, attr_name);
    emit_op(s, OP_define_field);
    emit_atom(s, attr_name);
    JS_FreeAtom(s->ctx, attr_name);

    if (next_web_token(s))
      return -1;
  }

#if defined(CONFIG_JSX_SCITER) // HTML shortcuts used by Sciter
  if (class_buffer[0]) { // add remaining classes 
    attr_value = JS_NewString(s->ctx, class_buffer);
    emit_push_const(s, JS_NewString(s->ctx, class_buffer), 0);
    JS_FreeValue(s->ctx, attr_value);
    attr_name = JS_NewAtom(s->ctx, "class");
    set_object_name(s, attr_name);
    emit_op(s, OP_define_field);
    emit_atom(s, attr_name);
    JS_FreeAtom(s->ctx, attr_name);
  }
#endif

  // parse content of the element
  
  for (;;) {
    const uint8_t *p;
    p = s->last_ptr = s->buf_ptr;
    s->last_line_num = s->token.line_num;
    if (js_parse_string(s, '<', TRUE, p, &s->token, &p))
      goto fail;
    if (s->buf_ptr != p) {
      s->buf_ptr = p;
      if (emit_push_const(s, s->token.u.str.str, 1))
        goto fail;
      ++kids_count;
    }
    next_token(s);
    if (s->token.val == '<') {
      if (*s->buf_ptr == '/') {
        next_token(s);        // skip '/'
        next_web_token(s);    // get tail tag name
        if (token_is_ident(s->token.val)) {  /* keywords and reserved words have a valid atom */
          if(s->token.u.ident.atom != tag_atom)
            return js_parse_error(s, "head and tail tags do not match");
          next_token(s);
          if (s->token.val != '>')
            return js_parse_error(s, "expecting '>' in tail tag");
          break;
        }
      }
      else {
        js_parse_jsx_expr(s, level + 1);
        ++kids_count;
      }
    }
    else if (s->token.val == '{') {
      if (next_token(s))
        goto fail;
      if (js_parse_assign_expr(s/*, TRUE*/))
        goto fail;
      if(s->token.val != '}')
        return js_parse_error(s, "expected '}'");
      ++kids_count;
    }
  }

GENERATE_KIDS:
  emit_op(s, OP_array_from);
  emit_u16(s, kids_count);
  
  emit_op(s, OP_call);
  emit_u16(s, 3);

  if (level == 0)
    next_token(s);

  return 0;
fail:
  JS_FreeValue(s->ctx, tag);
  JS_FreeAtom(s->ctx, attr_name);
  JS_FreeValue(s->ctx, attr_value);
  return -1;
}

