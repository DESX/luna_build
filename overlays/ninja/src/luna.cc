// Luna Build — Lua frontend for ninja.  See luna.h.
//
// Strategy (kept deliberately tiny): a .luna file is a Lua script. A small Lua
// prelude defines rule()/build()/variable()/default()/pool() which append
// equivalent *ninja manifest text* to a buffer via the one C primitive
// `_luna_emit`. After the script runs we feed the generated manifest to
// ninja's real ManifestParser, so every operation a build.ninja can express is
// available, with ninja doing all the parsing/graph/build work.

#include "luna.h"

#include <stdio.h>

#include "manifest_parser.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace {

// _luna_emit(str): append str to the manifest buffer (upvalue 1 = std::string*).
int luna_emit(lua_State* L) {
  std::string* buf =
      static_cast<std::string*>(lua_touserdata(L, lua_upvalueindex(1)));
  size_t len = 0;
  const char* s = luaL_checklstring(L, 1, &len);
  buf->append(s, len);
  return 0;
}

// Lua-side API. Translating tables -> ninja syntax in Lua keeps the C side to a
// single function and makes the surface easy to read and extend.
const char* kPrelude = R"LUA(
local function list(v)
  if v == nil then return '' end
  if type(v) == 'table' then return table.concat(v, ' ') end
  return tostring(v)
end
function variable(name, val) _luna_emit(name..' = '..tostring(val)..'\n') end
var = variable
local rule_order = {'command','description','depfile','deps',
  'msvc_deps_prefix','generator','restat','rspfile','rspfile_content','pool'}
function rule(name, opts)
  _luna_emit('rule '..name..'\n')
  opts = opts or {}
  local seen = {}
  for _, k in ipairs(rule_order) do
    if opts[k] ~= nil then _luna_emit('  '..k..' = '..tostring(opts[k])..'\n'); seen[k] = true end
  end
  for k, v in pairs(opts) do
    if not seen[k] then _luna_emit('  '..k..' = '..tostring(v)..'\n') end
  end
end
function build(a, b, c)
  local t = a
  if type(a) ~= 'table' or a.outputs == nil and a.rule == nil and a.inputs == nil then
    t = { outputs = a, rule = b, inputs = c }
  end
  local line = 'build '..list(t.outputs)
  if t.implicit_outputs then line = line..' | '..list(t.implicit_outputs) end
  line = line..': '..(t.rule or 'phony')
  if t.inputs then line = line..' '..list(t.inputs) end
  if t.implicit then line = line..' | '..list(t.implicit) end
  if t.order_only then line = line..' || '..list(t.order_only) end
  if t.validations then line = line..' |@ '..list(t.validations) end
  _luna_emit(line..'\n')
  if t.vars then
    for k, v in pairs(t.vars) do _luna_emit('  '..k..' = '..tostring(v)..'\n') end
  end
end
function default(...)
  local t = {...}
  if #t == 1 and type(t[1]) == 'table' then t = t[1] end
  _luna_emit('default '..table.concat(t, ' ')..'\n')
end
function pool(name, opts)
  _luna_emit('pool '..name..'\n')
  if opts and opts.depth then _luna_emit('  depth = '..tostring(opts.depth)..'\n') end
end
)LUA";

bool ReadFile(const std::string& path, std::string* out, std::string* err) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    *err = "luna: cannot open '" + path + "'";
    return false;
  }
  char chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
    out->append(chunk, n);
  fclose(f);
  return true;
}

}  // namespace

bool LunaLoad(ManifestParser& parser, const std::string& filename,
              std::string* err) {
  std::string script;
  if (!ReadFile(filename, &script, err))
    return false;

  std::string manifest;
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);

  lua_pushlightuserdata(L, &manifest);
  lua_pushcclosure(L, luna_emit, 1);
  lua_setglobal(L, "_luna_emit");

  if (luaL_dostring(L, kPrelude) != LUA_OK) {
    *err = std::string("luna internal prelude error: ") + lua_tostring(L, -1);
    lua_close(L);
    return false;
  }
  if (luaL_loadbuffer(L, script.data(), script.size(), filename.c_str()) !=
          LUA_OK ||
      lua_pcall(L, 0, 0, 0) != LUA_OK) {
    *err = std::string("luna: ") + lua_tostring(L, -1);
    lua_close(L);
    return false;
  }
  lua_close(L);

  // Hand the generated ninja manifest to ninja's own parser.
  return parser.ParseTest(manifest, err);
}
