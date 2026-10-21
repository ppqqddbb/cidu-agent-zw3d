    // =========================================================
    // 交互式参数选型表格（替换原富卡片列表）
    // 通用化：所有走 CFG_JY schema 的参数化模型都适用，不针对单一模型硬编码。
    // =========================================================

    function ciduIsSiloTopCutParameter(p) {
      const name = String(p && p.name || '')
        .replace(/\s+/g, '')
        .replace(/[（(【\[].*$/, '');
      return [
        '干渣仓仓顶是否切角',
        '干渣仓仓顶是否有切角',
        '渣仓仓体仓顶是否切角',
        '渣仓仓体仓顶是否有切角'
      ].includes(name);
    }

    function ciduSiloTopCutSemantic(value) {
      const raw = String(value == null ? '' : value).replace(/\s+/g, '');
      if (/不切角|不设切角|不需要切角/.test(raw) || raw === '否') return 'no-cut';
      if (/切角|需要切角/.test(raw) || raw === '是') return 'cut';
      return '';
    }

    function ciduSiloTopCutDisplayValue(value) {
      const semantic = ciduSiloTopCutSemantic(value);
      if (semantic === 'cut') return '切角';
      if (semantic === 'no-cut') return '不切角';
      return String(value == null ? '' : value);
    }

    function ciduSiloTopCutValueForOptions(value, options) {
      const semantic = ciduSiloTopCutSemantic(value);
      if (!semantic) return String(value == null ? '' : value);
      const normalized = (options || []).map((option) => String(option));
      const usesBooleanEnum = normalized.includes('是') && normalized.includes('否')
        && !normalized.includes('切角') && !normalized.includes('不切角');
      if (usesBooleanEnum) return semantic === 'cut' ? '是' : '否';
      return semantic === 'cut' ? '切角' : '不切角';
    }

    function ciduParamModel(manifest, options) {
      const params = (manifest && Array.isArray(manifest.params)) ? manifest.params.slice() : [];
      const tables = (manifest && manifest.tables) || {};
      const byGrid = new Map();
      const byName = new Map();
      const values = new Map();        // grid(Number) → 当前值(string)
      const userTouched = new Set();   // 权威回填或用户手动改过，防止模板公式重新覆盖
      const manuallyTouched = new Set(); // 仅记录用户在设计模型参数表中的显式修改
      const listeners = new Set();

      params.forEach((p) => {
        if (!p) return;
        if (p.grid != null) byGrid.set(Number(p.grid), p);
        if (p.name) byName.set(p.name, p);
        if (p.grid != null) {
          const def = (p.default == null) ? '' : String(p.default);
          values.set(Number(p.grid), def);
        }
      });

      function getValue(grid) {
        const v = values.get(Number(grid));
        return v == null ? '' : v;
      }
      function setValue(grid, v, opts) {
        const g = Number(grid);
        values.set(g, v == null ? '' : String(v));
        if (opts && opts.userTouched) userTouched.add(g);
        if (opts && opts.manualTouched) manuallyTouched.add(g);
        if (opts && opts.silent) return;
        notify(g);
      }
      function notify(changedGrid) {
        listeners.forEach((cb) => { try { cb(changedGrid); } catch (_) {} });
      }
      function onChange(cb) {
        listeners.add(cb);
        return () => listeners.delete(cb);
      }

      // ---------- 表达式求值 ----------
      function expandRefs(s) {
        return String(s == null ? '' : s).replace(/<grid(\d+)>/gi, function (_, n) {
          return getValue(Number(n));
        });
      }
      function evalExpr(s) {
        s = String(s == null ? '' : s).trim();
        if (!s) return '';
        if (s.indexOf('$IF$') >= 0) return evalIf(s);
        const fm = s.match(/^\{=(.+)\}$/);
        if (fm) return evalFormula(fm[1]);
        // 嵌入式 <gridN> 也要展开（DWSMQHCP.1.1.1 的 IF THEN 里有 "<grid128>.2-1 管板A" 这种）
        if (s.indexOf('<grid') >= 0) return expandRefs(s);
        return s;
      }
      function evalIf(s) {
        const m = s.match(/^\{\$IF\$([\s\S]+)\$ENDIF\$\}$/);
        if (!m) return s;
        const body = m[1];
        const fbIdx = body.lastIndexOf('|1=1:');
        let condThen, fallback;
        if (fbIdx >= 0) {
          condThen = body.slice(0, fbIdx);
          fallback = body.slice(fbIdx + 5);
        } else {
          condThen = body;
          fallback = '';
        }
        // 形如 cond1:then1  —— 简单切第一个 ":" 作为分隔
        const ci = condThen.indexOf(':');
        if (ci < 0) return '';
        const cond = condThen.slice(0, ci);
        const thenPart = condThen.slice(ci + 1);
        return evalCond(cond) ? evalExpr(thenPart) : evalExpr(fallback);
      }
      function splitOutsideBrackets(s, sep) {
        const out = [];
        let buf = '';
        let depth = 0;
        for (let i = 0; i < s.length; i++) {
          const c = s[i];
          if (c === '[' || c === '{' || c === '(') depth++;
          else if (c === ']' || c === '}' || c === ')') depth--;
          else if (c === sep && depth === 0) { out.push(buf); buf = ''; continue; }
          buf += c;
        }
        out.push(buf);
        return out;
      }
      function evalCond(cond) {
        cond = String(cond || '').trim();
        if (!cond) return false;
        // OR 优先级低于 AND；先按 | 和 & 拆（这两个字符不会出现在值里）
        const ors = splitOutsideBrackets(cond, '|');
        if (ors.length > 1) return ors.some(evalCond);
        const ands = splitOutsideBrackets(cond, '&');
        if (ands.length > 1) return ands.every(evalCond);
        // 必须先展开 <gridN> —— 否则 "<grid1>=A" 里的 ">=" 会被当成运算符 (1>=)
        const expanded = expandRefs(cond);
        const ops = ['>=', '<=', '!=', '=', '>', '<'];
        for (let i = 0; i < ops.length; i++) {
          const op = ops[i];
          const idx = expanded.indexOf(op);
          if (idx > 0) {
            const lhs = expanded.slice(0, idx).trim();
            const rhs = expanded.slice(idx + op.length).trim();
            return compareValues(lhs, op, rhs);
          }
        }
        return false;
      }
      function compareValues(lhs, op, rhs) {
        const ln = parseFloat(lhs);
        const rn = parseFloat(rhs);
        if (!isNaN(ln) && !isNaN(rn)) {
          switch (op) {
            case '=': return ln === rn;
            case '!=': return ln !== rn;
            case '>': return ln > rn;
            case '<': return ln < rn;
            case '>=': return ln >= rn;
            case '<=': return ln <= rn;
          }
        }
        switch (op) {
          case '=': return lhs === rhs;
          case '!=': return lhs !== rhs;
          case '>': return lhs > rhs;
          case '<': return lhs < rhs;
          case '>=': return lhs >= rhs;
          case '<=': return lhs <= rhs;
        }
        return false;
      }
      function evalFormula(expr) {
        let s = expandRefs(expr);
        s = s.replace(/\s+/g, '');
        // 去掉精度后缀如 ",in2" / ",in0"（CFG_JY 里用来标记小数位数，对求值无影响）
        s = s.replace(/,in\d+\s*$/i, '');
        s = s
          .replace(/\bSin\b/g, 'Math.sin')
          .replace(/\bCos\b/g, 'Math.cos')
          .replace(/\bTan\b/g, 'Math.tan')
          // CFG_JY 公式里 Atn() 是反正切（对应 VBA 的 Atn / JS 的 Math.atan）。
          // 实测 DTYC.GZJ440.渣井 的 grid3330 = Atn(<grid3324>/<grid3328>)*180/PI，
          // 不加这个映射，整条 grid3330→3332→3336→3338 都会算不出来。
          .replace(/\bAtn\b/g, 'Math.atan')
          .replace(/\bround\b/g, 'Math.round')
          .replace(/\brmin\b/g, 'Math.floor')
          .replace(/\brmax\b/g, 'Math.ceil')
          // CFG_JY 公式里 sqr() 是"求平方根"（不是平方），实测 grid39 = sqr(grid38)
          // 用 4642^2 - (1500-150)^2 ≈ 19.7M，sqr 后约 4444，与 grid41=rmin(grid39)→4444 链路吻合。
          .replace(/\bsqr\b/g, 'Math.sqrt')
          .replace(/\bPI\b/g, 'Math.PI');
        // CFG_JY 公式里 ^ 是幂运算（数学约定），JS 里 ^ 是按位异或——必须换成 **。
        // 实测 87/电动三通 的 grid215 = 2*430^2-2*(430^2)*Cos(PI/180*<grid210>)，
        // 不换会让 ^ 被白名单拒掉，整条 215→220→225→230(推杆行程) 链路算不出。
        s = s.replace(/\^/g, '**');
        if (!/^[0-9.+\-*/()A-Za-z_,]+$/.test(s)) return '';
        try {
          const v = Function('"use strict"; return (' + s + ')')();
          if (typeof v !== 'number' || isNaN(v)) return '';
          return String(v);
        } catch (_) {
          return '';
        }
      }

      function resolveType(p) {
        return evalExpr(p && p.type);
      }
      function resolveDefault(p) {
        return evalExpr(p && p.default);
      }
      function resolveRange(p) {
        const raw = String(p && p.range || '').trim();
        if (!raw) return null;
        // 支持的范围语法（任一端点可以是 <gridN> 引用）：
        //   {[min,max]}                经典闭区间
        //   {[min,max],+step}          带步长
        //   {(min,max]}                开下闭上（DSTYDF.5）
        //   {[min,max),+step}          闭下开上
        //   {(min,max),+step}          全开
        //   {[min,max],+step,水平方向}  step 后跟备注（DSTYDF.5）
        const m = raw.match(/^\{([\(\[])([^,]+),([^\)\]]+)([\)\]])(?:\s*,\s*\+?([^,\}]+))?(?:\s*,[^\}]*)?\}$/);
        if (!m) return null;
        const minOpen = m[1] === '(';
        const min = parseFloat(expandRefs(m[2]));
        const max = parseFloat(expandRefs(m[3]));
        const maxOpen = m[4] === ')';
        if (isNaN(min) && isNaN(max)) return null;
        const step = m[5] ? parseFloat(expandRefs(m[5])) : NaN;
        return { min: min, max: max, step: isNaN(step) ? null : step, minOpen: minOpen, maxOpen: maxOpen };
      }
      function parseRelationFilters(rel) {
        // 形如 [列名op值][列名op值]... ；值里可能含 <gridN> 引用
        // op 顺序按长度优先：>=, <=, !=, =, >, <
        const out = [];
        const re = /\[([^\]]+)\]/g;
        let m;
        while ((m = re.exec(rel)) !== null) {
          const inner = m[1];
          const ops = ['>=', '<=', '!=', '=', '>', '<'];
          for (let i = 0; i < ops.length; i++) {
            const op = ops[i];
            const idx = inner.indexOf(op);
            if (idx > 0) {
              out.push({ col: inner.slice(0, idx).trim(), op: op, valueExpr: inner.slice(idx + op.length).trim() });
              break;
            }
          }
        }
        return out;
      }

      function resolveValueList(p) {
        const raw = String(p && p.value_list || '').trim();
        if (!raw) return null;
        let s = raw;
        if (s.startsWith('{') && s.endsWith('}')) s = s.slice(1, -1);
        // 公式型
        if (s.startsWith('=')) {
          const v = evalFormula(s.slice(1));
          return v ? [v] : null;
        }
        // 表引用 [name]
        const tm = s.match(/^\[([^\]]+)\]$/);
        if (tm) {
          const tName = tm[1];
          // 显式 AltDataSource 优先于 [name]（一般两者一致，但 AltDataSource 是权威字段）
          const t = tables[String(p.alt_data_source || tName)] || tables[tName];
          if (!t || !Array.isArray(t.rows)) return [];
          const cols = Array.isArray(t.columns) ? t.columns : [];
          // 取值列：列名首选 [name] 里指的列；否则用 columns[0]
          const valueCol = (cols.indexOf(tName) >= 0) ? tName : (cols[0] || '');
          if (!valueCol) return [];
          // jyasm-service 写出来的 rows 是数组的数组（list[list]），按列下标取值；
          // 兼容对象形式（{列名: 值}）以防后端将来改格式。
          const getCell = (row, colName) => {
            if (row == null) return undefined;
            if (Array.isArray(row)) {
              const idx = cols.indexOf(colName);
              return idx >= 0 ? row[idx] : undefined;
            }
            return row[colName];
          };
          // 解析 RelationField 过滤条件（多条 AND 关系）
          let rows = t.rows;
          const rel = String(p && p.relation || '').trim();
          if (rel) {
            const filters = parseRelationFilters(rel);
            if (filters.length) {
              rows = rows.filter((row) => filters.every((f) => {
                const cellVal = getCell(row, f.col);
                const cellStr = cellVal == null ? '' : String(cellVal);
                const targetStr = expandRefs(f.valueExpr);
                return compareValues(cellStr, f.op, targetStr);
              }));
            }
          }
          const seen = new Set();
          const out = [];
          rows.forEach((row) => {
            const v = getCell(row, valueCol);
            if (v == null || v === '') return;
            const sv = String(v);
            if (seen.has(sv)) return;
            seen.add(sv);
            out.push(sv);
          });
          return out;
        }
        // 单个 grid 引用：{<gridN>} → 复制另一个值
        if (/^<grid\d+>$/i.test(s)) return [expandRefs(s)];
        // 范围 [min,max] / (min,max] 不算 select
        if (s.startsWith('[') || s.startsWith('(')) return null;
        // 列表 a,b,c — 每个 item 也可能含 <gridN> 引用
        if (s.indexOf(',') >= 0) {
          return s.split(',').map((x) => expandRefs(x.trim())).filter((x) => x);
        }
        // 分号列表 a;b;c（常见于参考表里如 {AH;WN;YY;ZW}）
        if (s.indexOf(';') >= 0) {
          return s.split(';').map((x) => expandRefs(x.trim())).filter((x) => x);
        }
        // 单值兜底：可能含嵌入 grid 引用如 "<grid124>-12 隔离桶A"
        const expanded = expandRefs(s);
        return expanded ? [expanded] : null;
      }
      function isVisible(p) {
        if (!p) return false;
        if (p.is_derived) return false;
        const name = String(p.name || '').trim();
        if (!name) return false;
        // CFG_JY 约定：grid 900~999 段为"模型属性"区（规格型号 / 物料编码 / 库存 等），
        // 由 ZW3D 原生 UI 独立显示在表格外，参数交互表不渲染。注意只是 900-999 这一段，
        // 1000+ 是正常参数（CDPT 仓顶平台的 grid 一路用到 6010）。
        const g = Number(p.grid);
        if (!isNaN(g) && g >= 900 && g <= 999) return false;
        const t = String(resolveType(p) || '').trim();
        // CFG_JY 约定：type 空 → "无编辑器"，是派生/计算列（版本号占位行、IF 算 0/1
        // 的内部布尔抑制位等），原生 ZW3D 表单也不显示，这里同步隐藏。
        if (!t) return false;
        if (t === 'Hidden') return false;
        return true;
      }
      // 用于"上传到 Excel / 提交 C#"场景：当前隐藏但 type 表达式存在非 Hidden 分支的参数
      // 也要并入，避免 Excel 列缺失。例如 DSTYDF.5 的"导流板厚度/材质/内撑杆规格"——
      // type={$IF$<grid_是否为风道>=否:Hidden|1=1:STRING_COMBO$ENDIF$}，当前"是否为风道=是"时
      // 这些是 Hidden 不显示在表格里，但 Excel 仍需要这些列以承接另一分支的值。
      function isPotentiallyVisible(p) {
        if (!p) return false;
        if (p.is_derived) return false;
        const name = String(p.name || '').trim();
        if (!name) return false;
        const g = Number(p.grid);
        if (!isNaN(g) && g >= 900 && g <= 999) return false;
        const rawType = String(p && p.type || '').trim();
        if (!rawType) return false;
        if (rawType === 'Hidden') return false;
        if (rawType.indexOf('$IF$') < 0) return true; // 静态非空非 Hidden
        // IF 表达式：实测各 jyasm 里出现的"非隐藏"类型字面量只有 Edit / STRING_COMBO / Disabled
        // （Number / CheckBox 等暂未见过，新增类型时在这里加即可）。
        return ['Edit', 'STRING_COMBO', 'Disabled'].some((t) => rawType.indexOf(t) >= 0);
      }

      // CFG_JY 的 default 列里常出现两种"未直接可用"的值：
      //   1) IF/公式/grid 引用表达式 — 需要按当前值递归求值，可能依赖其它 grid 故跑定点循环。
      //   2) 表列名占位 — 形如 default="渣仓直径" 且 value_list="{[渣仓直径]}"，Excel 作者用
      //      列名当"未选择"占位，ZW3D 原生表单遇到这种会自动选表第一行，所以我们也补一下。
      // 两类之间常有交叉依赖（如 斜梯.jyasm 的 grid26={=<grid10>*3}，grid10 是 DATA_TTB
      // 表占位），所以把"公式定点循环"和"表占位回填"再包一层外层定点，直到无变化为止。
      function resolveAllDefaults() {
        function runFormulaLoop() {
          let anyChanged = false;
          for (let iter = 0; iter < 10; iter++) {
            let changed = false;
            params.forEach((p) => {
              if (!p || p.grid == null) return;
              const g = Number(p.grid);
              if (userTouched.has(g)) return;
              const raw = String(p.default == null ? '' : p.default).trim();
              if (!raw) return;
              if (raw.indexOf('<grid') < 0 && raw.indexOf('$IF$') < 0 && raw.indexOf('{=') < 0) return;
              const newDef = evalExpr(raw);
              const curr = values.get(g);
              if (newDef !== '' && newDef !== curr) {
                values.set(g, newDef);
                changed = true;
                anyChanged = true;
              }
            });
            if (!changed) break;
          }
          return anyChanged;
        }
        function runTablePlaceholderLoop() {
          let changed = false;
          params.forEach((p) => {
            if (!p || p.grid == null) return;
            const g = Number(p.grid);
            if (userTouched.has(g)) return;
            const raw = String(p.default == null ? '' : p.default).trim();
            const vl = String(p.value_list == null ? '' : p.value_list).trim();
            if (!vl) return;
            const m = vl.match(/^\{\[([^\]]+)\]\}$/);
            if (!m) return;
            const colName = m[1];
            const nm = String(p.name || '').trim();
            // 已被公式循环算成非占位值的不再覆盖。
            const curr = values.get(g);
            if (curr && curr !== colName && curr !== nm && curr !== raw) return;
            if (raw && raw !== colName && raw !== nm) return;
            const list = resolveValueList(p);
            const next = (list && list.length) ? list[0] : '';
            if (next !== curr) {
              values.set(g, next);
              changed = true;
            }
          });
          return changed;
        }
        // 先回填表占位（grid10 等用作其它公式的输入），再跑公式循环，再交替直到稳定。
        for (let outer = 0; outer < 5; outer++) {
          const t = runTablePlaceholderLoop();
          const f = runFormulaLoop();
          if (!t && !f) break;
        }
      }

      if (!options || !options.deferDefaults) {
        resolveAllDefaults();
      }

      return {
        params: params,
        tables: tables,
        byGrid: byGrid,
        byName: byName,
        getValue: getValue,
        setValue: setValue,
        onChange: onChange,
        resolveAllDefaults: resolveAllDefaults,
        isUserTouched: function (g) { return userTouched.has(Number(g)); },
        isManuallyTouched: function (g) { return manuallyTouched.has(Number(g)); },
        resolveType: resolveType,
        resolveDefault: resolveDefault,
        resolveRange: resolveRange,
        resolveValueList: resolveValueList,
        isVisible: isVisible,
        isPotentiallyVisible: isPotentiallyVisible,
      };
    }

    function ciduOpenImage(url, title) {
      if (!url) return;
      const overlay = document.createElement('div');
      overlay.className = 'param-image-modal';
      const inner = document.createElement('div');
      inner.className = 'param-image-modal-inner';
      const img = document.createElement('img');
      img.src = url;
      img.alt = title || '';
      const cap = document.createElement('div');
      cap.className = 'param-image-modal-cap';
      cap.textContent = title || '';
      inner.appendChild(img);
      if (title) inner.appendChild(cap);
      overlay.appendChild(inner);
      overlay.addEventListener('click', () => overlay.remove());
      document.body.appendChild(overlay);
    }

    function ciduParamRawValue(p) {
      if (!p) return '';
      if (p.default != null && String(p.default).trim()) return String(p.default).trim();
      return '';
    }

    function ciduStripMaterialCodePrefix(value, prefix) {
      let text = String(value == null ? '' : value).trim();
      const code = String(prefix == null ? '' : prefix).trim();
      if (!text || !code) return text;
      if (text.toLowerCase().startsWith(code.toLowerCase())) {
        text = text.slice(code.length).replace(/^[\s._\-]+/, '').trim();
      }
      return text;
    }

    function ciduCleanSpecTemplateValue(value, prefix) {
      let text = String(value == null ? '' : value).trim();
      if (!text) return '';
      text = text.replace(/\{?\s*<grid\d+>\s*\}?/gi, '').replace(/^[\s._\-]+/, '').trim();
      return ciduStripMaterialCodePrefix(text, prefix);
    }

    function ciduExcelParameterName(p) {
      const name = String(p && p.name || '').trim();
      const unit = String(p && p.unit || '').trim();
      if (!name || !unit || /[\[【（(]/.test(name)) return name;
      return name + '[' + unit + ']';
    }

    function ciduSpecModelForExcel(model, parameters) {
      const specParam = model.byName.get('规格型号') || model.byGrid.get(999);
      const prefixParam = model.byName.get('物料编码前缀');
      const prefix = prefixParam
        ? model.getValue(prefixParam.grid)
        : (parameters && parameters['物料编码前缀']);
      // 使用页面上当前显示的值（包含物料编码前缀），不做任何清理
      const currentSpec = specParam ? model.getValue(specParam.grid) : '';
      return {
        spec_model_original: currentSpec,
        material_code_prefix: prefix == null ? '' : String(prefix)
      };
    }

    // 从 MAP_JY 表中查找 Excel 列名映射
    function lookupMapJY(tables, paramName) {
      const mapJY = tables && tables.MAP_JY;
      if (!mapJY || !Array.isArray(mapJY.rows) || !Array.isArray(mapJY.columns)) {
        return null;
      }
      const gridItemIndex = mapJY.columns.indexOf('Grid_Item_Name');
      const userVarIndex = mapJY.columns.indexOf('UserVariable');
      if (gridItemIndex < 0 || userVarIndex < 0) {
        return null;
      }
      for (const row of mapJY.rows) {
        if (Array.isArray(row) && row[gridItemIndex] === paramName) {
          return row[userVarIndex] || null;
        }
      }
      return null;
    }

    function ciduSubmitParamTable(model, result) {
      // 收集参数提交给 C# 写 Excel。范围比"当前可见"宽一点 —— 取"可能可见"的并集：
      // 当前隐藏但 type 表达式存在非 Hidden 分支的参数（如 DSTYDF.5 的"是否为风道=否"
      // 时被隐藏的导流板系列）也包含，避免分支切换后 Excel 缺列。当前隐藏的参数其值就是
      // resolveAllDefaults 算出来的默认值，不会"丢"。

      // 首先收集所有参数值（用 CFG_JY 参数名作为 key）
      const paramValuesByCfgName = {};
      model.params.forEach((p) => {
        if (!model.isPotentiallyVisible(p)) return;
        const cfgName = p.name || '';
        if (!cfgName) return;
        const v = model.getValue(p.grid);
        paramValuesByCfgName[cfgName] = v == null ? '' : String(v);
      });

      // 特殊处理：规格型号可能需要从特殊位置获取
      const specParam = model.byName.get('规格型号') || model.byGrid.get(999);
      if (specParam) {
        const specValue = model.getValue(specParam.grid);
        paramValuesByCfgName['规格型号'] = specValue == null ? '' : String(specValue);
      }

      // 如果有 MAP_JY 表，按 MAP_JY 的顺序生成 Excel 参数
      const mapJY = model.tables && model.tables.MAP_JY;
      const parameters = {};
      const parameter_order = [];
      const manifest_params = [];
      const manual_parameter_overrides = {};

      if (mapJY && Array.isArray(mapJY.rows) && Array.isArray(mapJY.columns)) {
        const gridItemIndex = mapJY.columns.indexOf('Grid_Item_Name');
        const userVarIndex = mapJY.columns.indexOf('UserVariable');
        if (gridItemIndex >= 0 && userVarIndex >= 0) {
          // 按 MAP_JY 的顺序遍历
          mapJY.rows.forEach((row) => {
            if (!Array.isArray(row)) return;
            const cfgName = row[gridItemIndex];
            const excelName = row[userVarIndex];
            if (!cfgName || !excelName) return;
            // 从收集的参数值中查找对应值
            const value = paramValuesByCfgName[cfgName] || '';
            parameters[excelName] = value;
            parameter_order.push(excelName);
            // 查找对应的参数定义获取单位信息
            const param = model.byName.get(cfgName);
            manifest_params.push({
              name: cfgName,
              unit: param && param.unit || '',
              excel_name: excelName
            });
            if (param && model.isManuallyTouched && model.isManuallyTouched(param.grid)) {
              manual_parameter_overrides[excelName] = value;
            }
          });
        }
      }

      // 如果没有 MAP_JY 或解析失败，提示错误并返回
      if (parameter_order.length === 0) {
        // 检查是否因为缺少 MAP_JY 表
        if (!mapJY || !Array.isArray(mapJY.rows) || !Array.isArray(mapJY.columns)) {
          alert('服务器 manifest.json 文件中缺少 MAP_JY 表定义，无法生成 Excel。请联系管理员检查配置。');
          return;
        }
        // 如果有 MAP_JY 但解析失败，尝试回退到原有逻辑
        const seen = new Set();
        model.params.forEach((p) => {
          if (!model.isPotentiallyVisible(p)) return;
          const excelName = ciduExcelParameterName(p);
          if (!excelName || seen.has(excelName)) return;
          seen.add(excelName);
          const v = model.getValue(p.grid);
          parameters[excelName] = v == null ? '' : String(v);
          parameter_order.push(excelName);
          manifest_params.push({
            name: p.name || '',
            unit: p.unit || '',
            excel_name: excelName
          });
          if (model.isManuallyTouched && model.isManuallyTouched(p.grid)) {
            manual_parameter_overrides[excelName] = parameters[excelName];
          }
        });
        // 如果回退逻辑也失败了，提示错误
        if (parameter_order.length === 0) {
          alert('无法生成 Excel：找不到可用的参数定义。');
          return;
        }
      }

      const specPayload = ciduSpecModelForExcel(model, parameters);
      // 把 result 里的选型上下文回填进 currentSelection（product_id 等可能后于
      // 模型确认时点才补齐，这里覆盖式更新避免文件名仍回退到模板名）。
      if (!currentSelection) currentSelection = {};
      const sel = result || {};
      if (sel.selected_model && !currentSelection.selected_model) currentSelection.selected_model = sel.selected_model;
      if (sel.template_name && !currentSelection.template_name) currentSelection.template_name = sel.template_name;
      if (sel.product_id && !currentSelection.product_id) currentSelection.product_id = sel.product_id;
      if (sel.selected_full_node_path && !currentSelection.selected_full_node_path) {
        currentSelection.selected_full_node_path = sel.selected_full_node_path;
      }
      // override 路径：sendJiavuAction 不再从对话抽 parameter_rows，确保 Excel 列与
      // 富卡片可见参数严格对齐。
      const parsedSelection = normalizeResultSelection(result) || {};
      const localSelection = Object.assign({}, currentSelection || {}, parsedSelection, {
        selected_model: sel.selected_model || sel.model_name || parsedSelection.selected_model || (currentSelection && currentSelection.selected_model) || '',
        template_name: sel.template_name || sel.selection_template_name || parsedSelection.template_name || (currentSelection && currentSelection.template_name) || '',
        selected_full_node_path: sel.selected_full_node_path || sel.full_node_path || sel.tree_path || parsedSelection.selected_full_node_path || (currentSelection && currentSelection.selected_full_node_path) || '',
        product_id: sel.product_id || sel.productId || parsedSelection.product_id || (currentSelection && currentSelection.product_id) || '',
        spec_model_original: specPayload.spec_model_original || '',
        spec_model: specPayload.spec_model_original || '',
        material_code_prefix: specPayload.material_code_prefix || ''
      });
      sendJiavuAction('generateModel', {
        parameters: parameters,
        parameter_order: parameter_order,
        manifest_params: manifest_params,
        manual_parameter_overrides: manual_parameter_overrides,
        selection: localSelection
      });
    }

    // 备注列显示规则（与 ZW3D 原生表单一致）：
    //   1) 优先 p.remark（去外层花括号、展开 <gridN> 引用）
    //   2) 否则用 range 渲染成 "[min,max], +step" 提示
    //   3) 都没有则空
    function formatRemark(p, model) {
      let raw = String(p && p.remark || '').trim();
      if (raw.startsWith('{') && raw.endsWith('}')) raw = raw.slice(1, -1).trim();
      if (raw) {
        // remark 里也可能含 <gridN> 引用
        if (raw.indexOf('<grid') >= 0 && model && typeof model.resolveDefault === 'function') {
          // 借用 resolveDefault 的 expandRefs 逻辑：构造临时 param
          try { raw = model.resolveDefault({ default: raw }) || raw; } catch (_) {}
        }
        return raw;
      }
      const r = model && model.resolveRange ? model.resolveRange(p) : null;
      if (!r) return '';
      const lb = r.minOpen ? '(' : '[';
      const rb = r.maxOpen ? ')' : ']';
      const minS = isNaN(r.min) ? '' : String(r.min);
      const maxS = isNaN(r.max) ? '' : String(r.max);
      const stepS = (r.step != null && !isNaN(r.step)) ? ', +' + r.step : '';
      return lb + minS + ',' + maxS + rb + stepS;
    }

    function renderParameterTable(container, manifest, result) {
      if (!container || !manifest) return false;
      const params = manifest.params || [];
      if (!params.length) return false;

      const model = ciduParamModel(manifest, { deferDefaults: true });
      // 设计模型只允许最近一次“开始计算”响应中的“输入参数核对”和
      // “设备选型结果”参与自动回填。禁止历史对话、sessionStorage、模型确认响应
      // 以及旧的混合候选池先行写值。
      const appliedAutofill = {};
      const autofillDebug = {};
      if (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge) {
        const bridgeResult = ciduParameterBridge.applyDesignModel(model, result)
          || { applied: {}, debug: {} };
        Object.assign(appliedAutofill, bridgeResult.applied || {});
        autofillDebug.parameterBridge = bridgeResult.debug || {};
        autofillDebug.entries = Number(bridgeResult.debug && bridgeResult.debug.extraction || 0)
          + Number(bridgeResult.debug && bridgeResult.debug.equipment || 0);
      }
      model.resolveAllDefaults();
      syncAutofilledModelParametersToSelection(model, true);

      // 至少有 1 个 visible 才走表格；否则交给兜底卡片
      const visibleCount = params.reduce((acc, p) => acc + (model.isVisible(p) ? 1 : 0), 0);
      if (!visibleCount) return false;

      // 按 step_page 分组（0/null → 1，11 → "高级"）
      const pages = new Map();
      params.forEach((p) => {
        let page = Number(p.step_page);
        if (!page || isNaN(page)) page = 1;
        if (!pages.has(page)) pages.set(page, []);
        pages.get(page).push(p);
      });

      // 分页器以"当前真正可见"为准 —— 一个 step 只剩 is_derived/Hidden 时，
      // 表格内只会显示"本步骤参数全部由系统自动计算，无需修改"占位，对用户没有
      // 任何可操作内容，按用户反馈这种 step 不应该出现在分页器里。值变化时
      // model.onChange → renderFooter → orderedPages 会自动重算，原本被隐藏的
      // step 在依赖参数被切换后会重新冒出来。
      const pageHasVisible = (list) => list.some((p) => model.isVisible(p));
      const orderedPages = () => Array.from(pages.keys())
        .filter((p) => pageHasVisible(pages.get(p)))
        .sort((a, b) => a - b);

      // 移除老的参数说明块（兜底卡片用同样的 dataset 标记）
      const prev = container.querySelector('[data-parameter-requirements="true"]');
      if (prev) prev.remove();

      const details = document.createElement('details');
      details.className = 'details param-table-details';
      details.dataset.parameterRequirements = 'true';
      details.dataset.paramTable = 'true';
      details.open = true;

      const summary = document.createElement('summary');
      const appliedCount = Object.keys(appliedAutofill).length;
      if (appliedCount) {
        summary.textContent = '参数说明（已从最新计算结果回填 ' + appliedCount + ' 项）';
      } else if (autofillDebug.entries) {
        summary.textContent = '参数说明（读取到最新计算结果 ' + autofillDebug.entries + ' 条，未匹配到同名参数）';
      } else {
        summary.textContent = '参数说明（未读取到设备选型结果，请先执行「开始计算」）';
      }
      details.appendChild(summary);

      const body = document.createElement('div');
      body.className = 'details-body';
      details.appendChild(body);

      // ZW3D 原生底部是 |< 步骤N >| 居中，顶部不再单独放 tab 行。
      const tableWrap = document.createElement('div');
      tableWrap.className = 'param-table-wrap';
      body.appendChild(tableWrap);

      const footer = document.createElement('div');
      footer.className = 'param-table-footer';
      body.appendChild(footer);

      // 列宽（参数名称 / 参数值 / 备注 / 图）— 用户可通过表头分隔条拖拽调整，
      // 写入 localStorage 持久化。defaults 与 CSS 默认 (40/28/24/8) 一致。
      const COL_DEFAULTS = [0.40, 0.28, 0.24, 0.08];
      const COL_MIN_RATIO = 0.06;
      const COL_STORAGE_KEY = 'cidu.paramTableColRatios';
      let columnRatios = COL_DEFAULTS.slice();
      try {
        const saved = JSON.parse(localStorage.getItem(COL_STORAGE_KEY) || 'null');
        if (Array.isArray(saved) && saved.length === 4
            && saved.every((x) => typeof x === 'number' && x > 0 && x < 1)) {
          // 归一化（防止旧版本写入未做归一）
          const sum = saved.reduce((a, b) => a + b, 0);
          if (sum > 0.5 && sum < 1.5) {
            columnRatios = saved.map((x) => x / sum);
          }
        }
      } catch (_) {}
      const persistColumnRatios = () => {
        try { localStorage.setItem(COL_STORAGE_KEY, JSON.stringify(columnRatios)); } catch (_) {}
      };
      const applyColumnWidths = (ths) => {
        for (let i = 0; i < ths.length && i < columnRatios.length; i++) {
          ths[i].style.width = (columnRatios[i] * 100).toFixed(3) + '%';
        }
      };
      const attachResizers = (table) => {
        const ths = table.querySelectorAll('thead th');
        applyColumnWidths(ths);
        for (let i = 0; i < ths.length - 1; i++) {
          const handle = document.createElement('div');
          handle.className = 'col-resizer';
          ths[i].appendChild(handle);
          handle.addEventListener('mousedown', (ev) => {
            ev.preventDefault();
            ev.stopPropagation();
            handle.classList.add('resizing');
            document.body.style.cursor = 'col-resize';
            const startX = ev.clientX;
            const tableWidth = table.getBoundingClientRect().width || 1;
            const leftStart = columnRatios[i];
            const rightStart = columnRatios[i + 1];
            const combined = leftStart + rightStart;
            const onMove = (mv) => {
              const dRatio = (mv.clientX - startX) / tableWidth;
              let left = leftStart + dRatio;
              let right = rightStart - dRatio;
              if (left < COL_MIN_RATIO) { left = COL_MIN_RATIO; right = combined - COL_MIN_RATIO; }
              if (right < COL_MIN_RATIO) { right = COL_MIN_RATIO; left = combined - COL_MIN_RATIO; }
              columnRatios[i] = left;
              columnRatios[i + 1] = right;
              ths[i].style.width = (left * 100).toFixed(3) + '%';
              ths[i + 1].style.width = (right * 100).toFixed(3) + '%';
            };
            const onUp = () => {
              document.removeEventListener('mousemove', onMove);
              document.removeEventListener('mouseup', onUp);
              document.body.style.cursor = '';
              handle.classList.remove('resizing');
              persistColumnRatios();
            };
            document.addEventListener('mousemove', onMove);
            document.addEventListener('mouseup', onUp);
          });
          // 双击恢复该分隔条相邻两列的默认比例
          handle.addEventListener('dblclick', (ev) => {
            ev.preventDefault();
            const sum = columnRatios[i] + columnRatios[i + 1];
            const defSum = COL_DEFAULTS[i] + COL_DEFAULTS[i + 1];
            if (defSum <= 0) return;
            columnRatios[i] = sum * (COL_DEFAULTS[i] / defSum);
            columnRatios[i + 1] = sum * (COL_DEFAULTS[i + 1] / defSum);
            applyColumnWidths(ths);
            persistColumnRatios();
          });
        }
      };

      // 整体总览图 + 规格型号：参考 ZW3D 原生选型框底部的产品 3D 缩略图，
      // 上方显示规格型号（grid 900-999 段属于"模型属性"，主表格里被过滤）。
      const allImages = Array.isArray(manifest.images) ? manifest.images : [];
      const refByParams = new Set();
      (manifest.params || []).forEach((p) => {
        const n = (p && p.image ? String(p.image).trim() : '');
        if (n) refByParams.add(n);
      });
      const heroEntry = allImages.length
        ? (allImages.find((it) => it && it.name && !refByParams.has(String(it.name).trim())) || allImages[0])
        : null;
      const heroUrl = heroEntry && (heroEntry.url || heroEntry.rel_path)
        ? jyasmResolveImageUrl(heroEntry.url || heroEntry.rel_path)
        : '';

      const specParam = model.byName.get('规格型号') || model.byGrid.get(999);
      let heroSpecValueEl = null;
      const refreshHeroSpec = () => {
        if (!heroSpecValueEl || !specParam) return;
        const v = model.getValue(specParam.grid);
        heroSpecValueEl.textContent = (v == null || v === '') ? '-' : String(v);
      };

      if (heroUrl || specParam) {
        const heroBox = document.createElement('div');
        heroBox.className = 'param-table-hero';
        if (specParam) {
          const specRow = document.createElement('div');
          specRow.className = 'param-table-hero-spec';
          const label = document.createElement('span');
          label.className = 'param-table-hero-spec-label';
          label.textContent = '规格型号';
          const value = document.createElement('span');
          value.className = 'param-table-hero-spec-value';
          specRow.appendChild(label);
          specRow.appendChild(value);
          heroBox.appendChild(specRow);
          heroSpecValueEl = value;
          refreshHeroSpec();
        }
        if (heroUrl) {
          const heroImg = document.createElement('img');
          heroImg.src = heroUrl;
          heroImg.alt = (heroEntry && heroEntry.name) || '';
          heroImg.addEventListener('click', () => ciduOpenImage(heroUrl, (heroEntry && heroEntry.name) || ''));
          heroBox.appendChild(heroImg);
        }
        body.appendChild(heroBox);
      }

      let currentPage = null;

      // 保持 currentPage 在有效页范围内（值变化导致页消失时自动回到第一页）。
      const ensureValidPage = () => {
        const pgs = orderedPages();
        if (!pgs.length) { currentPage = null; return pgs; }
        if (currentPage === null || pgs.indexOf(currentPage) < 0) currentPage = pgs[0];
        return pgs;
      };

      const buildControl = (p) => {
        const type = model.resolveType(p);
        const grid = Number(p.grid);
        const curr = model.getValue(grid);

        if (p.is_derived || type === 'Disabled') {
          const span = document.createElement('span');
          span.className = 'param-table-readonly';
          span.textContent = curr || '—';
          return span;
        }

        const opts = (type === 'STRING_COMBO') ? model.resolveValueList(p) : null;
        if (opts && opts.length) {
          const isSiloTopCut = ciduIsSiloTopCutParameter(p);
          const selectedValue = isSiloTopCut
            ? ciduSiloTopCutValueForOptions(curr, opts)
            : curr;
          if (isSiloTopCut && selectedValue !== curr) {
            model.setValue(grid, selectedValue, { silent: true });
          }
          const sel = document.createElement('select');
          sel.className = 'param-table-input';
          // 当前值若不在 options 里，先把它当一项 + 默认选中
          const seen = new Set();
          const all = [];
          if (selectedValue && !opts.includes(selectedValue)) all.push(selectedValue);
          opts.forEach((o) => { if (!seen.has(o)) { seen.add(o); all.push(o); } });
          all.forEach((o) => {
            const opt = document.createElement('option');
            opt.value = String(o);
            opt.textContent = isSiloTopCut ? ciduSiloTopCutDisplayValue(o) : String(o);
            if (String(o) === selectedValue) opt.selected = true;
            sel.appendChild(opt);
          });
          sel.addEventListener('change', () => {
            model.setValue(grid, sel.value, { userTouched: true, manualTouched: true });
          });
          return sel;
        }

        const input = document.createElement('input');
        input.className = 'param-table-input';
        input.type = 'text';
        input.value = curr;
        const range = model.resolveRange(p);
        if (range) {
          input.type = 'number';
          if (!isNaN(range.min)) input.min = String(range.min);
          if (!isNaN(range.max)) input.max = String(range.max);
          if (range.step != null && !isNaN(range.step)) input.step = String(range.step);
          input.dataset.range = JSON.stringify(range);
        }
        const validate = () => {
          const row = input.closest('tr');
          if (!row) return;
          if (!input.dataset.range) return;
          const r = JSON.parse(input.dataset.range);
          const v = parseFloat(input.value);
          if (isNaN(v)) { row.classList.remove('range-invalid'); return; }
          const overMin = !isNaN(r.min) && (r.minOpen ? v <= r.min : v < r.min);
          const overMax = !isNaN(r.max) && (r.maxOpen ? v >= r.max : v > r.max);
          if (overMin || overMax) {
            row.classList.add('range-invalid');
            const remarkTd = row.querySelector('.param-table-remark');
            if (remarkTd) {
              const lb = r.minOpen ? '(' : '[';
              const rb = r.maxOpen ? ')' : ']';
              remarkTd.setAttribute('data-range-hint', '范围 ' + lb + (isNaN(r.min) ? '' : r.min) + ', ' + (isNaN(r.max) ? '' : r.max) + rb);
            }
          } else {
            row.classList.remove('range-invalid');
          }
        };
        input.addEventListener('input', validate);
        input.addEventListener('change', () => {
          model.setValue(grid, input.value, { userTouched: true, manualTouched: true });
        });
        return input;
      };

      const buildRow = (p) => {
        if (!model.isVisible(p)) return null;
        const tr = document.createElement('tr');
        tr.dataset.grid = String(p.grid);

        const nameTd = document.createElement('td');
        const unit = p.unit ? ' [' + p.unit + ']' : '';
        nameTd.textContent = (p.name || '') + unit;
        tr.appendChild(nameTd);

        const valTd = document.createElement('td');
        valTd.appendChild(buildControl(p));
        tr.appendChild(valTd);

        const remarkTd = document.createElement('td');
        remarkTd.className = 'param-table-remark';
        const autofillInfo = appliedAutofill[p.name || String(p.grid)];
        const remarkText = formatRemark(p, model);
        const autofillSourceValue = autofillInfo && autofillInfo.sourceValue != null
          ? autofillInfo.sourceValue
          : autofillInfo && autofillInfo.value;
        remarkTd.textContent = autofillInfo
          ? (remarkText ? remarkText + '；' : '') + '回填自 ' + autofillInfo.source + '=' + autofillSourceValue
          : remarkText;
        tr.appendChild(remarkTd);

        const imgTd = document.createElement('td');
        imgTd.className = 'param-table-img-cell';
        if (p.image_url) {
          const btn = document.createElement('button');
          btn.type = 'button';
          btn.className = 'param-table-img-btn';
          // 内嵌 SVG 替代原 🖼 emoji —— Windows / Qt WebView2 默认字体缺彩色 emoji 表，
          // 之前会渲染成方框/口字。SVG 用 currentColor 跟随主题色，hover 时变 accent。
          btn.innerHTML = '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="9" cy="10" r="1.6"/><path d="M21 16l-5-5-8 8"/></svg>';
          btn.title = '查看图';
          const url = jyasmResolveImageUrl(p.image_url);
          btn.addEventListener('click', (e) => { e.stopPropagation(); ciduOpenImage(url, p.name); });
          imgTd.appendChild(btn);
        }
        tr.appendChild(imgTd);

        return tr;
      };

      const renderRows = () => {
        tableWrap.innerHTML = '';
        if (currentPage === null) {
          const empty = document.createElement('div');
          empty.style.cssText = 'padding:14px;color:var(--muted);text-align:center;';
          empty.textContent = '当前没有需要修改的参数。';
          tableWrap.appendChild(empty);
          return;
        }
        const list = pages.get(currentPage) || [];
        const table = document.createElement('table');
        table.className = 'param-table';
        const thead = document.createElement('thead');
        thead.innerHTML = '<tr><th>参数名称</th><th>参数值</th><th>备注</th><th></th></tr>';
        table.appendChild(thead);
        const tbody = document.createElement('tbody');
        list.forEach((p) => {
          const row = buildRow(p);
          if (row) tbody.appendChild(row);
        });
        if (!tbody.children.length) {
          const tr = document.createElement('tr');
          const td = document.createElement('td');
          td.colSpan = 4;
          td.style.cssText = 'padding:12px;color:var(--muted);text-align:center;';
          td.textContent = '本步骤参数全部由系统自动计算，无需修改。';
          tr.appendChild(td);
          tbody.appendChild(tr);
        }
        table.appendChild(tbody);
        tableWrap.appendChild(table);
        // 列宽 inline 应用 + 给表头加可拖拽分隔条（双击复位该分隔条相邻两列）
        attachResizers(table);
      };

      // 步骤压缩显示规则（仿 GitHub 分页器，按用户给的样例）：
      //   显示集合 = {1, current, N}；current=1 时多带 2，current=N 时多带 N-1。
      //   相邻两项之间数字断档时用 "..." 占位。
      const buildStepList = (total, currentIdx1Based) => {
        if (total <= 1) return [{ type: 'step', n: 1 }];
        const set = new Set([1, currentIdx1Based, total]);
        if (currentIdx1Based === 1) set.add(2);
        if (currentIdx1Based === total) set.add(total - 1);
        const sorted = Array.from(set).filter((x) => x >= 1 && x <= total).sort((a, b) => a - b);
        const out = [];
        for (let i = 0; i < sorted.length; i++) {
          if (i > 0 && sorted[i] > sorted[i - 1] + 1) out.push({ type: 'ellipsis' });
          out.push({ type: 'step', n: sorted[i] });
        }
        return out;
      };

      const renderFooter = () => {
        footer.innerHTML = '';
        const pgs = ensureValidPage();
        const idx = pgs.indexOf(currentPage);
        const total = pgs.length;

        // [上一步]  步骤1 步骤2 ... 步骤N  [下一步]      [生成模型]
        const nav = document.createElement('div');
        nav.className = 'param-table-step-nav';

        const prevBtn = document.createElement('button');
        prevBtn.type = 'button';
        prevBtn.className = 'param-table-step-arrow';
        prevBtn.textContent = '上一步';
        prevBtn.disabled = idx <= 0;
        prevBtn.addEventListener('click', () => {
          if (idx > 0) {
            currentPage = pgs[idx - 1];
            renderRows(); renderFooter();
          }
        });
        nav.appendChild(prevBtn);

        const stepBox = document.createElement('div');
        stepBox.className = 'param-table-step-list';
        buildStepList(total, idx + 1).forEach((item) => {
          if (item.type === 'ellipsis') {
            const dots = document.createElement('span');
            dots.className = 'param-table-step-ellipsis';
            dots.textContent = '...';
            stepBox.appendChild(dots);
          } else {
            const btn = document.createElement('button');
            btn.type = 'button';
            const active = (item.n - 1) === idx;
            btn.className = 'param-table-step-chip' + (active ? ' active' : '');
            btn.textContent = '步骤' + item.n;
            btn.disabled = active;
            btn.addEventListener('click', () => {
              currentPage = pgs[item.n - 1];
              renderRows(); renderFooter();
            });
            stepBox.appendChild(btn);
          }
        });
        nav.appendChild(stepBox);

        const nextBtn = document.createElement('button');
        nextBtn.type = 'button';
        nextBtn.className = 'param-table-step-arrow';
        nextBtn.textContent = '下一步';
        nextBtn.disabled = idx < 0 || idx >= total - 1;
        nextBtn.addEventListener('click', () => {
          if (idx >= 0 && idx < total - 1) {
            currentPage = pgs[idx + 1];
            renderRows(); renderFooter();
          }
        });
        nav.appendChild(nextBtn);

        footer.appendChild(nav);

        const submit = document.createElement('button');
        submit.type = 'button';
        submit.className = 'param-table-submit';
        submit.textContent = '生成模型';
        submit.addEventListener('click', () => ciduSubmitParamTable(model, result));
        footer.appendChild(submit);
      };

      // 联动：值变化时重算所有未被用户改过的 default（含 IF / grid 引用 / 公式 / 表列名占位）
      model.onChange(() => {
        model.resolveAllDefaults();
        syncAutofilledModelParametersToSelection(model, true);
        renderRows();
        renderFooter();
        refreshHeroSpec();
      });

      ensureValidPage();
      renderRows();
      if (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge) {
        ciduParameterBridge.applyRenderedTable(tableWrap, model, appliedAutofill);
      }
      renderFooter();
      container.appendChild(details);
      return true;
    }
