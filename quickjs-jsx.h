
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

static int is_non_space_run(const uint8_t* start, const uint8_t* end) {
  for (; start < end; ++start) if (!lre_is_space(*start)) break;
  for (; end > start; --end) if (!lre_is_space(*(end-1))) break;
  return end - start;
}

static int invalid_name_token(int t) {
  //return t != TOK_IDENT && !(t >= TOK_IF && t <= TOK_OF);
  return !token_is_ident(t);
}

static int js_parse_jsx_expr(JSParseState *s, int level)
{
  int atts_count = 0;
  int kids_count = 0;
  JSAtom  tag_atom = JS_ATOM_NULL;
  JSValue tag = JS_UNINITIALIZED;
  JSAtom  attr_name = JS_ATOM_NULL;
  JSValue attr_value = JS_UNINITIALIZED;

  const char* errmsg = "invalid JSX expression";
#if defined(CONFIG_JSX_SCITER) // HTML shortcuts used by Sciter
  char class_buffer[512] = { 0 };
#endif

  // NOTE: caller already consumed '<' 
  if (next_web_token(s)) goto fail;
  if (invalid_name_token(s->token.val)) {
    errmsg = "Expecting tag name";
    goto fail;
  }

  //tag
  tag_atom = JS_DupAtom(s->ctx,s->token.u.ident.atom);
  tag = JS_AtomToString(s->ctx,tag_atom);

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
  }
  else {
    if (emit_push_const(s, tag, 0))
      goto fail;
  }

  // parse attributes

  if (next_web_token(s)) goto fail;
  
  emit_op(s, OP_object);

  while (s->token.val != '>') {
    
    if (s->token.val == '/') {
      if (next_token(s))
        goto fail;
      //json_parse_expect(s, '>');
      if (s->token.val != '>') {
        errmsg = "expecting '>'";
        goto fail;
      }
      goto GENERATE_KIDS;
    }
#if defined(CONFIG_JSX_SCITER) // HTML shortcuts used by Sciter
    if (s->token.val == '#') { // <div #some> ->  <div id="some">
      if (next_web_token(s)) goto fail;
      if (invalid_name_token(s->token.val)) {
        errmsg = "expecting identifier";
        goto fail;
      }
      attr_name = JS_NewAtom(s->ctx,"id");
      attr_value = JS_AtomToString(s->ctx, s->token.u.ident.atom);
      goto PUSH_ATTR_VALUE;
    }
    if (s->token.val == '|') { // <input|text> ->  <input type="text">
      if (next_web_token(s)) goto fail;
      if (invalid_name_token(s->token.val)) {
        errmsg = "expecting identifier";
        goto fail;
      }
      attr_name = JS_NewAtom(s->ctx, "type");
      attr_value = JS_AtomToString(s->ctx, s->token.u.ident.atom);
      goto PUSH_ATTR_VALUE;
    }
    if (s->token.val == '(') { // <input(foo)> ->  <input name="foo">
      if (next_web_token(s)) goto fail;
      if (invalid_name_token(s->token.val)) {
        errmsg = "expecting identifier";
        goto fail;
      }
      attr_name = JS_NewAtom(s->ctx, "name");
      attr_value = JS_AtomToString(s->ctx, s->token.u.ident.atom);
      if (next_token(s)) goto fail;
      if (s->token.val != ')') { 
        errmsg = "expecting identifier";
        goto fail;
      }
      goto PUSH_ATTR_VALUE;
    }
    if (s->token.val == '.') { // <div.some> ->  <div class="some">
      if (next_web_token(s)) goto fail;
      if (invalid_name_token(s->token.val)) {
        errmsg = "expecting identifier";
        goto fail;
      }
      char cls1[256];
      const char *name = JS_AtomGetStr(s->ctx, cls1, countof(cls1), s->token.u.ident.atom);
      if (strlen(class_buffer) + strlen(name) + 2 < countof(class_buffer)) {
        if(class_buffer[0]) strcat(class_buffer, " ");
        strcat(class_buffer, name);
      }
      if (next_web_token(s)) goto fail;
      continue;
    }
#endif

    if (s->token.val == '{') // <a {atts}>foo</a>
    {
      if (next_token(s))
        goto fail;
      if (js_parse_assign_expr(s))
        goto fail;
      if (s->token.val != '}') {
        errmsg = "expecting '}'";
        goto fail;
      }
      //attr_name = JS_NewAtomUInt32(s->ctx, unnamed_atts_count++);
      emit_op(s, OP_null);  /* dummy excludeList */
      emit_op(s, OP_copy_data_properties);
      emit_u8(s, 2 | (1 << 2) | (0 << 5));
      emit_op(s, OP_drop); /* pop excludeList */
      emit_op(s, OP_drop); /* pop src object */

      if (next_web_token(s))
        goto fail;

      continue;
    } 
    else if (token_is_ident(s->token.val)) 
    {
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
      if (js_parse_assign_expr(s))
        goto fail;
      if (s->token.val != '}') {
        errmsg = "expecting '}'";
        goto fail;
      }
    }

    set_object_name(s, attr_name);
    emit_op(s, OP_define_field);
    emit_atom(s, attr_name);
    JS_FreeAtom(s->ctx, attr_name);

    if (next_web_token(s))
      goto fail;
  }

#if defined(CONFIG_JSX_SCITER) // HTML shortcuts used by Sciter
  if (class_buffer[0]) { // add remaining classes 
    attr_value = JS_NewString(s->ctx, class_buffer);
    int r = emit_push_const(s,attr_value, 0);
    JS_FreeValue(s->ctx, attr_value);
    if (r < 0) goto fail;
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
      const uint8_t *start = s->buf_ptr;
      s->buf_ptr = p;
      if (is_non_space_run(start, p)) {
        JSValue str = JS_NewStringLen(s->ctx, start, p - start);
        if(str == JS_EXCEPTION)
        goto fail;
        if (emit_push_const(s,str, 1)) {
          JS_FreeValue(s->ctx, str);
          goto fail;
        }
        JS_FreeValue(s->ctx, str);
      ++kids_count;
    }
    }
    if (next_token(s))
      goto fail;

    if (s->token.val == '<') {
      if (*s->buf_ptr == '/') {
        if (next_token(s)) // skip '/'
          goto fail;
        if (next_web_token(s)) // get tail tag name
          goto fail;
        if (token_is_ident(s->token.val)) {  /* keywords and reserved words have a valid atom */
          if (s->token.u.ident.atom != tag_atom) {
            errmsg = "head and tail tags do not match";
            goto fail;
          }
          if (next_token(s))
            goto fail;
          if (s->token.val != '>') {
            errmsg = "expecting '>' in tail tag";
            goto fail;
          }
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
      if (js_parse_assign_expr(s))
        goto fail;
      if (s->token.val != '}') {
        errmsg = "expected '}'";
        goto fail;
      }
      ++kids_count;
    }
  }

GENERATE_KIDS:
  emit_op(s, OP_array_from);
  emit_u16(s, kids_count);
  
  emit_op(s, OP_call);
  emit_u16(s, 3);

  if (level == 0) {
    if (next_token(s))
      goto fail;
  }

  JS_FreeValue(s->ctx, tag);
  JS_FreeAtom(s->ctx, tag_atom);
  //JS_FreeAtom(s->ctx, attr_name);
  //JS_FreeValue(s->ctx, attr_value);

  return 0;
fail:
  JS_FreeValue(s->ctx, tag);
  JS_FreeAtom(s->ctx, tag_atom);
  JS_FreeAtom(s->ctx, attr_name);
  //JS_FreeValue(s->ctx, attr_value);
  return js_parse_error(s, errmsg);
}

