    // 解析 Dify 工作流藏在 markdown 末尾的 <!-- cidu_selection_meta: {...} --> 块。
    // 现在只含 product_id：前端拿到后直连 jyasm-service /products/{id}/manifest 拉富参数。
    // 旧版本还藏过 parameter_requirements_struct，但 Dify 知识库 800 字符硬切把 JSON 切碎了，已废弃。
    function parseSelectionMetaFromMarkdown(markdown) {
      const text = String(markdown || '');
      const match = text.match(/<!--\s*cidu_selection_meta:\s*([\s\S]*?)\s*-->/);
      if (!match) return {};
      try {
        const parsed = JSON.parse(match[1].trim());
        return parsed && typeof parsed === 'object' ? parsed : {};
      } catch (e) {
        return {};
      }
    }

    function parseAllSelectionMetaFromMarkdown(markdown) {
      const text = String(markdown || '');
      const metas = [];
      const pattern = /<!--\s*cidu_selection_meta:\s*([\s\S]*?)\s*-->/g;
      let match;
      while ((match = pattern.exec(text)) !== null) {
        try {
          const parsed = JSON.parse(match[1].trim());
          if (parsed && typeof parsed === 'object') metas.push(parsed);
        } catch (_) { /* ignore broken meta */ }
      }
      return metas;
    }

    function hasMultipleSelectionBlocks(markdown) {
      return parseSelectionsFromMarkdown(markdown).length > 1;
    }

    function hasAnySelectionBlock(markdown) {
      return parseSelectionsFromMarkdown(markdown).length > 0;
    }

    function isCandidateListResponseMarkdown(markdown) {
      const text = String(markdown || '');
      if (!text) return false;
      return /##\s*请选择模型/.test(text)
        || /请(?:直接)?回复编号/.test(text)
        || /确认后我再询问该模型需要的参数/.test(text)
        || /请选择(?:以下)?(?:一个|多个)?模型/.test(text);
    }

    function isConfirmedSelectionMarkdown(markdown) {
      const text = String(markdown || '');
      if (!text) return false;
      return /##\s*已确认(?:多个)?模型/.test(text)
        || /本次将同时设计\s*\d+\s*个模型/.test(text)
        || /本次将设计(?:以下)?(?:模型)?/.test(text);
    }

    function confirmedSelectionMarkdownSlice(markdown) {
      const text = String(markdown || '').replace(/\r\n/g, '\n');
      const startPatterns = [
        /(?:^|\n)\s*#{1,6}\s*已确认(?:多个)?模型[^\n]*/g,
        /(?:^|\n)\s*本次将同时设计\s*\d+\s*个模型[^\n]*/g,
        /(?:^|\n)\s*本次将设计(?:以下)?(?:模型)?[^\n]*/g
      ];
      let start = -1;
      for (const pattern of startPatterns) {
        const match = pattern.exec(text);
        if (match) {
          const index = match.index + (match[0].startsWith('\n') ? 1 : 0);
          start = start < 0 ? index : Math.min(start, index);
        }
      }
      let sliced = start >= 0 ? text.slice(start) : text;
      const stopPatterns = [
        /\n\s*#{1,6}\s*识别到的检索线索\b/,
        /\n\s*#{1,6}\s*检索线索\b/,
        /\n\s*#{1,6}\s*候选模型\b/,
        /\n\s*#{1,6}\s*请选择模型\b/,
        /\n\s*(?:识别到的检索线索|检索线索)\s*[：:]/
      ];
      let stop = -1;
      for (const pattern of stopPatterns) {
        const match = sliced.match(pattern);
        if (match && match.index > 0) {
          stop = stop < 0 ? match.index : Math.min(stop, match.index);
        }
      }
      if (stop > 0) sliced = sliced.slice(0, stop);
      return sliced.trim();
    }

    // 是否"模型选型确认"响应 —— 富卡片渲染（含 parameter_requirements 卡片列表 + jyasm 拉富卡片）
    // 仅在这种响应上触发，技术协议处理链路（参数提取 / 公式计算 / 参数修改回显等）的中间响应
    // 不属于此范畴，避免在中间响应里画出空值的"模型需要的参数"和"缺少 product_id"诊断条。
    function isSelectionResponse(result) {
      if (!result) return false;
      const md = String(result.answer_markdown || '');
      if (md && isCandidateListResponseMarkdown(md) && !isConfirmedSelectionMarkdown(md)) return false;
      if (result.selection_confirmed === true || result.selectionConfirmed === true) return true;
      if (md && isConfirmedSelectionMarkdown(md)) return true;
      if (result.product_id || result.productId) return true;
      if (!md) return false;
      const meta = parseSelectionMetaFromMarkdown(md);
      if (meta && (meta.selection_confirmed === true || meta.selectionConfirmed === true)) return true;
      return !!(meta && (meta.product_id || meta.productId) && !isCandidateListResponseMarkdown(md));
    }

    // 把 jyasm-service /products /manifest 等内网 URL 拼成完整地址。
    // 优先用设置里的 base_url（局域网快），CORS 头由 jyasm-service 主动放开。
    function jyasmServiceBaseUrl() {
      const base = String(currentSettings.jyasm_service_base_url || '').trim();
      return base.replace(/\/+$/, '');
    }

    function jyasmServiceFetch(path) {
      const base = jyasmServiceBaseUrl();
      if (!base) return Promise.reject(new Error('未配置 jyasm-service 内网地址'));
      const headers = {};
      const token = String(currentSettings.jyasm_service_token || '').trim();
      if (token) headers['Authorization'] = 'Bearer ' + token;
      const timeoutMs = parseInt(currentSettings.jyasm_service_timeout_ms, 10) || 900000;
      const controller = new AbortController();
      const timer = window.setTimeout(() => controller.abort(), timeoutMs);
      return fetch(base + path, { headers, signal: controller.signal })
        .then((r) => {
          window.clearTimeout(timer);
          if (!r.ok) throw new Error('jyasm-service HTTP ' + r.status);
          return r.json();
        })
        .catch((err) => {
          window.clearTimeout(timer);
          throw err;
        });
    }

    // 列表里按 source_jyasm basename 反查 productId（去掉 .jyasm 后缀做精确比对，再做去空白模糊匹配）。
    let jyasmProductsCache = null;
    let jyasmProductsCacheAt = 0;
    function jyasmListProducts() {
      const now = Date.now();
      if (jyasmProductsCache && now - jyasmProductsCacheAt < 60000) {
        return Promise.resolve(jyasmProductsCache);
      }
      return jyasmServiceFetch('/products').then((list) => {
        if (Array.isArray(list)) {
          jyasmProductsCache = list;
          jyasmProductsCacheAt = now;
        }
        return Array.isArray(list) ? list : [];
      });
    }

    function numericProductId(value) {
      const text = String(value == null ? '' : value).trim();
      return /^\d+$/.test(text) ? text : '';
    }

    function cleanJyasmLookupName(value) {
      return String(value || '')
        .replace(/^\s*(?:模型|模型ID|模型\s*ID|模板|模板名称|候选模型)\s*[：:]\s*/i, '')
        .split(/[\\/]/).pop()
        .replace(/\.jy(?:asm|par)$/i, '')
        .trim();
    }

    function compactJyasmLookupName(value) {
      return cleanJyasmLookupName(value).replace(/\s+/g, '').toLowerCase();
    }

    function jyasmFindProductIdByName(list, selectedModel) {
      const target = cleanJyasmLookupName(selectedModel);
      if (!target) return '';
      const targetCompact = compactJyasmLookupName(target);
      for (const item of list || []) {
        const src = String(item && item.source_jyasm || '');
        const base = cleanJyasmLookupName(src);
        const baseCompact = compactJyasmLookupName(base);
        const pid = numericProductId(item && item.productId);
        if (!pid) continue;
        if (base === target) return pid;
        if (baseCompact === targetCompact) return pid;
        if (targetCompact.length >= 4 && baseCompact.indexOf(targetCompact) >= 0) return pid;
      }
      return '';
    }

    function jyasmResolveImageUrl(rel) {
      const s = String(rel || '').trim();
      if (!s) return '';
      if (/^https?:\/\//i.test(s)) return s;
      const base = jyasmServiceBaseUrl();
      if (!base) return '';
      return base + (s.startsWith('/') ? s : '/' + s);
    }

    // manifest.params 数组 → {name: {unit, range, remark, default, image_url, visibility, type_expr}}
    // 同时过滤 visibility==="hidden" —— 前端永远不展示。
    function jyasmManifestToStruct(manifest) {
      const out = {};
      const params = manifest && Array.isArray(manifest.params) ? manifest.params : [];
      const seen = new Set();
      for (const p of params) {
        const name = String((p && p.name) || '').trim();
        if (!name || seen.has(name)) continue;
        if (String(p.visibility || '').toLowerCase() === 'hidden') continue;
        seen.add(name);
        out[name] = {
          unit: p.unit || '',
          range: p.range || '',
          remark: p.remark || '',
          default: p.default || '',
          image: p.image || '',
          image_url: jyasmResolveImageUrl(p.image_url),
          step_page: p.step_page || 0,
          visibility: p.visibility || 'always',
          type_expr: p.type || '',
        };
      }
      return out;
    }

    // 异步：拿到 result 里的 product_id（或按 selected_model 反查）→ 拉 manifest → 重渲染富卡片。
    // 失败时在 container 末尾贴一行红色小字，方便排查（WebView2 没默认 DevTools）。
    function jyasmEnrichDebug(container, msg) {
      if (!container) return;
      try {
        const prev = container.querySelector('[data-jyasm-enrich-debug="true"]');
        if (prev) prev.remove();
        const node = document.createElement('div');
        node.dataset.jyasmEnrichDebug = 'true';
        node.style.cssText = 'margin-top:8px;font-size:12px;color:#c33;opacity:.8;';
        node.textContent = '[富卡片诊断] ' + String(msg || '');
        container.appendChild(node);
      } catch (_) { /* swallow */ }
    }
    function enrichSingleParameterTableFromJyasmService(container, result, options = {}) {
      const selection = options.selection || normalizeResultSelection(result) || {};
      const productId = numericProductId(selection.product_id)
        || numericProductId(result.product_id)
        || numericProductId(result.productId);
      const selectedModel = String(
        selection.selected_model || selection.template_name || result.selected_model || result.template_name || ''
      ).trim();
      if (!productId && !selectedModel) {
        // 选型响应里 product_id 和 selected_model 都没解析出来 —— chatflow 输出有问题，
        // 但不再渲染到气泡里污染生产 UI；保留 console.warn 给开发者排查。
        console.warn('[cidu] selection response missing product_id and selected_model');
        return;
      }
      if (!jyasmServiceBaseUrl()) {
        jyasmEnrichDebug(container, '设置里未填 jyasm_service_base_url');
        renderFallbackEditableParameterTable(container, result, selection);
        return;
      }
      const resolvePid = productId
        ? Promise.resolve(productId)
        : jyasmListProducts().then((list) => {
            const pid = jyasmFindProductIdByName(list, selection.template_name || selectedModel)
              || jyasmFindProductIdByName(list, selectedModel);
            if (!pid) {
              const sample = (list || []).slice(0, 5).map((it) => String(it && it.source_jyasm || '').split(/[\\/]/).pop()).join(' | ');
              jyasmEnrichDebug(container, '在 /products 列表里没找到 ' + selectedModel + '（前几个样本：' + sample + '）');
            }
            return pid;
          });
      resolvePid
        .then((pid) => {
          if (!pid) {
            jyasmEnrichDebug(container, '已识别模型，但没有匹配到 jyasm-service 产品ID，无法渲染可编辑参数表。');
            renderFallbackEditableParameterTable(container, result, selection);
            return null;
          }
          // 反查到的 pid 回写到 currentSelection，否则后续"生成模型"无法按
          // "产品id_产品名.xlsx" 生成佳鱼导入文件。
          if (currentSelection && !numericProductId(currentSelection.product_id)) {
            currentSelection.product_id = String(pid);
          }
          selection.product_id = String(pid);
          return jyasmServiceFetch('/products/' + encodeURIComponent(pid) + '/manifest');
        })
        .then((manifest) => {
          if (!manifest) return;
          if (!document.body.contains(container)) return;
          if (typeof rememberEquipmentSelectionFromChatDom === 'function') {
            rememberEquipmentSelectionFromChatDom();
          }
          // 优先用交互式表格渲染（支持参数联动、分步、可编辑）。
          // 渲染器返回 false 时（manifest 为空 / 无 visible 参数 / 解析异常）走兜底卡片。
          let rendered = false;
          try {
            rendered = renderParameterTable(container, manifest, Object.assign({}, result, selection));
          } catch (err) {
            jyasmEnrichDebug(container, '表格渲染异常，回退卡片：' + (err && err.message ? err.message : String(err)));
            rendered = false;
          }
          if (rendered) return;
          const struct = jyasmManifestToStruct(manifest);
          if (!Object.keys(struct).length) {
            jyasmEnrichDebug(container, 'manifest.params 为空或全部 hidden');
            renderFallbackEditableParameterTable(container, result, selection);
            return;
          }
          appendParameterRequirementDetails(container, Object.assign({}, result, {
            answer_markdown: '',
            selected_model: selection.selected_model || result.selected_model,
            template_name: selection.template_name || result.template_name,
            selected_full_node_path: selection.selected_full_node_path || result.selected_full_node_path,
            product_id: selection.product_id || result.product_id,
            parameter_requirements_struct: struct,
          }));
        })
        .catch((err) => {
          jyasmEnrichDebug(container, '请求出错：' + (err && err.message ? err.message : String(err)));
          renderFallbackEditableParameterTable(container, result, selection);
        });
    }

    function buildMultiSelectionContainer(container, selections) {
      const previous = container.querySelector('[data-multi-param-tables="true"]');
      if (previous) previous.remove();
      const wrap = document.createElement('div');
      wrap.className = 'multi-param-tables';
      wrap.dataset.multiParamTables = 'true';
      selections.forEach((selection, index) => {
        const block = document.createElement('section');
        block.className = 'multi-model-param-block';
        block.dataset.modelIndex = String(index + 1);
        const head = document.createElement('div');
        head.className = 'multi-model-param-head';
        const title = document.createElement('div');
        title.className = 'multi-model-param-title';
        title.textContent = '模型 ' + (index + 1) + '：' + (selection.selected_model || modelNameFromTemplateName(selection.template_name) || '未命名模型');
        head.appendChild(title);
        const subtitleParts = [];
        if (selection.template_name) subtitleParts.push(selection.template_name);
        if (selection.product_id) subtitleParts.push('产品ID ' + selection.product_id);
        if (subtitleParts.length) {
          const subtitle = document.createElement('div');
          subtitle.className = 'multi-model-param-subtitle';
          subtitle.textContent = subtitleParts.join(' / ');
          head.appendChild(subtitle);
        }
        block.appendChild(head);
        wrap.appendChild(block);
      });
      container.appendChild(wrap);
      return Array.from(wrap.querySelectorAll('.multi-model-param-block'));
    }

    function renderFallbackEditableParameterTable(container, result, selection) {
      if (!container) return false;
      const existing = container.querySelector('[data-fallback-param-table="true"]');
      if (existing) existing.remove();
      const parameters = filterParameterMap(Object.assign(
        {},
        result && result.numeric || {},
        result && result.text || {},
        result && result.answer_markdown ? parseMarkdownParameters(result.answer_markdown) : {}
      ));
      const entries = Object.entries(parameters).filter(([key, value]) => {
        if (!key || isIgnoredParameterName(key)) return false;
        const normalized = normalizeParamKey(key);
        if (['模型', '模型id', '模板名称', '模板名', '完整节点线路', '置信度', '检索来源'].some((name) => normalized.includes(normalizeParamKey(name)))) {
          return false;
        }
        return value != null && String(value).trim() !== '';
      });
      if (!entries.length) return false;

      const details = document.createElement('details');
      details.className = 'details param-table-details';
      details.dataset.parameterRequirements = 'true';
      details.dataset.fallbackParamTable = 'true';
      details.open = true;

      const summary = document.createElement('summary');
      summary.textContent = '参数说明';
      details.appendChild(summary);

      const body = document.createElement('div');
      body.className = 'details-body';
      details.appendChild(body);

      const tableWrap = document.createElement('div');
      tableWrap.className = 'param-table-wrap';
      const table = document.createElement('table');
      table.className = 'param-table';
      table.innerHTML = '<thead><tr><th>参数名称</th><th>参数值</th><th>备注</th><th></th></tr></thead>';
      const tbody = document.createElement('tbody');
      entries.forEach(([key, value]) => {
        const tr = document.createElement('tr');
        const nameTd = document.createElement('td');
        nameTd.textContent = key;
        const valueTd = document.createElement('td');
        const input = document.createElement('input');
        input.className = 'param-table-input';
        input.type = 'text';
        input.value = String(value == null ? '' : value);
        input.dataset.paramName = key;
        valueTd.appendChild(input);
        const remarkTd = document.createElement('td');
        remarkTd.className = 'param-table-remark';
        remarkTd.textContent = '来自设备选型结果/参数模板';
        const imgTd = document.createElement('td');
        imgTd.className = 'param-table-img-cell';
        tr.append(nameTd, valueTd, remarkTd, imgTd);
        tbody.appendChild(tr);
      });
      table.appendChild(tbody);
      tableWrap.appendChild(table);
      body.appendChild(tableWrap);

      const footer = document.createElement('div');
      footer.className = 'param-table-footer';
      const hint = document.createElement('div');
      hint.className = 'param-table-step-nav';
      hint.textContent = '未拉到完整 manifest，已使用当前识别参数生成可编辑表。';
      const submit = document.createElement('button');
      submit.type = 'button';
      submit.className = 'param-table-submit';
      submit.textContent = '生成模型';
      submit.addEventListener('click', () => {
        const params = {};
        const order = [];
        details.querySelectorAll('input[data-param-name]').forEach((input) => {
          const name = input.dataset.paramName || '';
          if (!name || isIgnoredParameterName(name)) return;
          params[name] = input.value == null ? '' : String(input.value);
          order.push(name);
        });
        sendJiavuAction('generateModel', {
          parameters: params,
          parameter_order: order,
          selection: Object.assign({}, currentSelection || {}, selection || normalizeResultSelection(result) || {})
        });
      });
      footer.append(hint, submit);
      body.appendChild(footer);
      container.appendChild(details);
      return true;
    }

    function enrichParameterRequirementsFromJyasmService(container, result) {
      if (!container || !result) return;
      // 同 appendParameterRequirementDetails：技术协议处理链路的中间响应不查询 jyasm-service。
      // 避免出现"[基础对话节点] 缺少 product_id 与 selected_model"这种诊断条暴露到生产 UI。
      if (!isSelectionResponse(result)) return;
      const selections = result.answer_markdown ? parseSelectionsFromMarkdown(result.answer_markdown) : [];
      if (selections.length > 1) {
        if (!jyasmServiceBaseUrl()) {
          jyasmEnrichDebug(container, '设置里未填 jyasm_service_base_url');
          return;
        }
        const blocks = buildMultiSelectionContainer(container, selections);
        blocks.forEach((block, index) => {
          enrichSingleParameterTableFromJyasmService(block, result, { selection: selections[index] });
        });
        return;
      }
      enrichSingleParameterTableFromJyasmService(container, result, {
        selection: selections[0] || normalizeResultSelection(result) || {}
      });
    }

    function parseSelectionFromMarkdown(markdown) {
      const text = String(markdown || '');
      if (isCandidateListResponseMarkdown(text) && !isConfirmedSelectionMarkdown(text)) return null;
      const getLineValue = (labels) => {
        for (const label of labels) {
          const escaped = label.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
          const match = text.match(new RegExp(`(?:\\*\\*)?${escaped}(?:\\*\\*)?\\s*[：:]\\s*([^\\n\\r]+)`, 'i'));
          if (match) return stripMarkdown(match[1]);
        }
        return '';
      };
      const selectedModel = getLineValue(['模型', '候选模型']);
      const templateName = getLineValue(['模板名称', '模板']);
      const fullNodePath = getLineValue(['完整节点线路', '完整节点路径', '完整路径', '节点线路', '树路径', 'tree_path', 'full_node_path']);
      // product_id 优先取藏在末尾的 meta 块（DSL 显式注入）；正文 "产品ID: 12345" 行作为兜底。
      const meta = parseSelectionMetaFromMarkdown(text);
      const productId = numericProductId(meta && meta.product_id)
        || numericProductId(getLineValue(['产品ID', '产品Id', '产品id', 'product_id', 'productId']));
      if (!selectedModel && !templateName && !fullNodePath) return null;
      return {
        selected_model: selectedModel || templateName.replace(/\.jyasm$/i, ''),
        template_name: templateName,
        selected_full_node_path: fullNodePath,
        product_id: productId,
        parameters: {}
      };
    }

    function normalizeSelectionTextValue(value) {
      return stripMarkdown(value)
        .replace(/^[-*]\s*/, '')
        .replace(/^\s*`+|`+\s*$/g, '')
        .trim();
    }

    function splitMarkdownSelectionBlocks(markdown) {
      const text = String(markdown || '').replace(/\r\n/g, '\n');
      const matches = [];
      const pattern = /(?:^|\n)\s*(?:#{1,6}\s*)?(?:\*\*)?\s*模型\s*(\d+|[一二三四五六七八九十]+)\s*[：:]\s*(.+?)(?:\*\*)?\s*(?=\n|$)/g;
      let match;
      while ((match = pattern.exec(text)) !== null) {
        matches.push({
          index: match.index + (match[0].startsWith('\n') ? 1 : 0),
          label: match[1],
          title: normalizeSelectionTextValue(match[2])
        });
      }
      if (!matches.length) return [];
      return matches.map((item, index) => {
        const end = index + 1 < matches.length ? matches[index + 1].index : text.length;
        return {
          label: item.label,
          title: item.title,
          text: text.slice(item.index, end).trim()
        };
      });
    }

    function parseSelectionFieldFromBlock(blockText, labels) {
      const text = String(blockText || '');
      for (const label of labels) {
        const escaped = label.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
        const patterns = [
          new RegExp(`(?:\\*\\*)?${escaped}(?:\\*\\*)?\\s*[：:]\\s*([^\\n\\r]+)`, 'i'),
          new RegExp(`^\\s*[-*]\\s*${escaped}\\s*[：:]\\s*([^\\n\\r]+)`, 'im')
        ];
        for (const pattern of patterns) {
          const match = text.match(pattern);
          if (match) return normalizeSelectionTextValue(match[1]);
        }
      }
      return '';
    }

    function modelNameFromTemplateName(templateName) {
      return normalizeSelectionTextValue(String(templateName || '').split(/[\\/]/).pop().replace(/\.jyasm$/i, ''));
    }

    function extractTemplateNamesFromMarkdown(markdown) {
      const text = String(markdown || '');
      const names = [];
      const add = (value) => {
        const cleaned = normalizeSelectionTextValue(value);
        if (!cleaned || !/\.jyasm$/i.test(cleaned)) return;
        if (!names.some((name) => normalizeParamKey(name) === normalizeParamKey(cleaned))) names.push(cleaned);
      };
      const fieldPattern = /(?:模板名称|模板名|模板|模型\s*ID|模型ID|template_name|model_id)\s*[：:]\s*([^\n\r]+?\.jyasm)/gi;
      let match;
      while ((match = fieldPattern.exec(text)) !== null) add(match[1]);
      const jyasmPattern = /([\w\u4e00-\u9fff（）()·.\-\s]+?\.jyasm)/gi;
      while ((match = jyasmPattern.exec(text)) !== null) {
        const raw = normalizeSelectionTextValue(match[1]).split(/\s+/).pop();
        add(raw);
      }
      return names;
    }

    function compactSelectionIdentity(selection) {
      return [
        selection && selection.product_id,
        selection && selection.template_name,
        selection && selection.selected_model,
        selection && selection.selected_full_node_path
      ].map((value) => normalizeParamKey(value)).join('|');
    }

    function parseSelectionsFromMarkdown(markdown) {
      const text = String(markdown || '');
      if (!text) return [];
      if (isCandidateListResponseMarkdown(text) && !isConfirmedSelectionMarkdown(text)) return [];
      const confirmedText = isConfirmedSelectionMarkdown(text) ? confirmedSelectionMarkdownSlice(text) : text;
      const metas = parseAllSelectionMetaFromMarkdown(confirmedText);
      const blocks = splitMarkdownSelectionBlocks(confirmedText);
      const selections = [];
      if (blocks.length) {
        blocks.forEach((block, index) => {
          const meta = metas[index] || {};
          const templateName = parseSelectionFieldFromBlock(block.text, ['模板名称', '模板', 'template_name', '模板名称/文件'])
            || parseSelectionFieldFromBlock(block.text, ['模型 ID', '模型ID', 'model_id']);
          const selectedModel = parseSelectionFieldFromBlock(block.text, ['模型', '候选模型', '模型名称', 'model_name'])
            || block.title
            || modelNameFromTemplateName(templateName);
          const fullNodePath = parseSelectionFieldFromBlock(block.text, ['完整节点线路', '完整节点路径', '完整路径', '节点线路', '树路径', 'tree_path', 'full_node_path']);
          const productId = numericProductId(meta && (meta.product_id || meta.productId))
            || numericProductId(parseSelectionFieldFromBlock(block.text, ['产品ID', '产品Id', '产品id', 'product_id', 'productId']));
          const selection = {
            selected_model: selectedModel || modelNameFromTemplateName(templateName),
            template_name: templateName,
            selected_full_node_path: fullNodePath,
            product_id: productId,
            parameters: {}
          };
          if (selection.selected_model || selection.template_name || selection.selected_full_node_path || selection.product_id) {
            selections.push(selection);
          }
        });
      } else {
        const single = parseSelectionFromMarkdown(confirmedText);
        if (single) {
          // 单模型确认时，"完整节点线路"里也可能包含其他 .jyasm 文件名。
          // 这些只是路径/检索线索，不是用户额外选择的模型，不能再全局扫描追加成模型 2。
          selections.push(single);
        } else {
          const fallbackTemplates = extractTemplateNamesFromMarkdown(confirmedText);
          const isConfirmedMultiple = /##\s*已确认多个模型/.test(text)
            || /本次将同时设计\s*\d+\s*个模型/.test(text);
          (isConfirmedMultiple ? fallbackTemplates : fallbackTemplates.slice(0, 1)).forEach((templateName) => {
            if (selections.some((selection) => normalizeParamKey(selection.template_name) === normalizeParamKey(templateName))) return;
            selections.push({
              selected_model: modelNameFromTemplateName(templateName),
              template_name: templateName,
              selected_full_node_path: '',
              product_id: '',
              parameters: {}
            });
          });
        }
      }

      const seen = new Set();
      return selections.filter((selection) => {
        const key = compactSelectionIdentity(selection);
        if (!key || seen.has(key)) return false;
        seen.add(key);
        return true;
      });
    }

    function normalizeResultSelection(result) {
      if (!result) return null;
      const markdownSelections = result.answer_markdown ? parseSelectionsFromMarkdown(result.answer_markdown) : [];
      const fromMarkdown = markdownSelections[0] || null;
      // parameter_requirements_struct 是 Dify 工作流通过 markdown 末尾 HTML 注释回传的结构化参数表
      // (含 image_url / range / remark / visibility)，前端用它渲染富卡片，避免在富文本里二次解析。
      const meta = result.answer_markdown ? parseSelectionMetaFromMarkdown(result.answer_markdown) : {};
      const struct = result.parameter_requirements_struct
        || (meta && meta.parameter_requirements_struct)
        || null;
      const selected = {
        selected_model: result.selected_model || (fromMarkdown && fromMarkdown.selected_model) || '',
        template_name: result.template_name || result.selection_template_name || (fromMarkdown && fromMarkdown.template_name) || '',
        selected_full_node_path: result.selected_full_node_path || result.full_node_path || result.node_full_path || result.model_full_path || result.tree_path || result.node_path || (fromMarkdown && fromMarkdown.selected_full_node_path) || '',
        // product_id 用于生成佳鱼要求的 Excel 文件名（产品id_产品名.xlsx）；
        // Dify 显式字段优先，回退到 markdown 解析。
        product_id: numericProductId(result.product_id) || numericProductId(result.productId) || numericProductId(fromMarkdown && fromMarkdown.product_id),
        parameter_requirements_struct: struct,
        parameters: filterParameterMap(Object.assign(
          {},
          currentSelection && currentSelection.parameters || {},
          (meta && meta.equipment_selection_parameters) || {},
          result.numeric || {},
          result.text || {},
          result.answer_markdown ? parseMarkdownParameters(result.answer_markdown) : {}
        )),
        parameter_rows: currentSelection && Array.isArray(currentSelection.parameter_rows)
          ? currentSelection.parameter_rows.slice()
          : [],
        equipment_result_rows: currentSelection && Array.isArray(currentSelection.equipment_result_rows)
          ? currentSelection.equipment_result_rows.slice()
          : []
      };
      if (!selected.selected_model && !selected.template_name && !selected.selected_full_node_path) {
        return null;
      }
      return selected;
    }

    function collectRequestedParameterNames(markdown) {
      const names = [];
      const text = String(markdown || '');
      const pattern = /^[-*]\s+\*\*?([^*：:]+)\*\*?[：:]/gm;
      let match;
      while ((match = pattern.exec(text)) !== null) {
        const name = stripMarkdown(match[1]);
        if (name && !isIgnoredParameterName(name) && !names.includes(name)) names.push(name);
      }
      return names;
    }

    function rememberRequestedParameters(result) {
      if (!result || !result.answer_markdown) return;
      const names = collectRequestedParameterNames(result.answer_markdown);
      const parsedValues = parseMarkdownParameters(result.answer_markdown);
      if ((!names.length && !Object.keys(parsedValues).length) || !currentSelection) return;
      const existing = filterParameterMap(currentSelection.parameters || {});
      names.forEach((name) => {
        if (!(name in existing)) existing[name] = '';
      });
      Object.keys(parsedValues).forEach((name) => {
        if (!isIgnoredParameterName(name)) existing[name] = parsedValues[name];
      });
      currentSelection.parameters = existing;
      if (!Array.isArray(currentSelection.parameter_rows) || !currentSelection.parameter_rows.length) {
        currentSelection.parameter_rows = [Object.assign({}, existing)];
      }
    }

    function extractParameterValues(prompt) {
      const text = String(prompt || '');
      const values = {};
      const knownParameters = currentSelection && currentSelection.parameters
        ? Object.keys(filterParameterMap(currentSelection.parameters))
        : [];
      text.split(/\r?\n/).forEach((line) => {
        const match = line.match(/^\s*(?:[-*]\s*)?([^=：:\n\r]{1,64})\s*[=：:]\s*(.+?)\s*$/);
        if (!match) return;
        const rawKey = stripMarkdown(match[1]);
        const value = stripMarkdown(match[2]);
        if (!rawKey || !value || isIgnoredParameterName(rawKey)) return;
        const known = findKnownParameterName(rawKey, knownParameters);
        values[known || rawKey] = value;
      });
      knownParameters.forEach((canonical) => {
        for (const key of parameterNameAliases(canonical)) {
          const match = text.match(parameterValuePattern(key));
          if (match) {
            values[canonical] = stripMarkdown(match[1]);
            return;
          }
        }
      });
      const genericPattern = /([A-Za-z0-9_.\-\u4e00-\u9fff（）()\[\]]{2,32})\s*[：:=]\s*([A-Za-z0-9_.×xX\-\u4e00-\u9fff]+)|([A-Za-z0-9_.\-\u4e00-\u9fff（）()\[\]]{2,32})\s+([A-Za-z0-9_.×xX\-\u4e00-\u9fff]+)/g;
      let match;
      while ((match = genericPattern.exec(text)) !== null) {
        const rawKey = stripMarkdown(match[1] || match[3]);
        const value = stripMarkdown(match[2] || match[4]);
        if (!rawKey || !value || isIgnoredParameterName(rawKey)) continue;
        const known = findKnownParameterName(rawKey, knownParameters);
        values[known || rawKey] = value;
      }
      knownParameters.forEach((name) => {
        const compact = normalizeParamKey(name);
        if (!compact.includes('底板')) return;
        if (/有底板|底板\s*有/.test(text)) values[name] = '有';
        if (/无底板|没有底板|底板\s*无/.test(text)) values[name] = '无';
      });
      return values;
    }
    function updateJiavuActions(selection, persist = true) {
      if (selection) {
        currentSelection = Object.assign({}, currentSelection || {}, selection);
        currentSelection.product_id = numericProductId(currentSelection.product_id);
      }
      if (persist && currentSelection) {
        const conversation = activeConversation();
        if (conversation) {
          conversation.selection = Object.assign({}, currentSelection);
          conversation.updated_at = Date.now();
          storageWrite();
        }
      }
      // 旧版底部 #jiavuActions（选中模型 / 生成模型）已删除；
      // currentSelection 仍然维护，供富卡片里的"生成模型"按钮使用。
    }

    function messageTextForParameterScan(message) {
      if (!message) return '';
      if (typeof message.text === 'string') return message.text;
      if (message.payload) {
        return [
          message.payload.answer_markdown || '',
          message.payload.message || ''
        ].filter(Boolean).join('\n\n');
      }
      return '';
    }

    function refreshSelectionParametersFromConversation() {
      if (!currentSelection) return;
      const conversation = activeConversation();
      const messages = conversation && Array.isArray(conversation.messages) ? conversation.messages : [];
      for (let index = messages.length - 1; index >= 0; index -= 1) {
        const text = messageTextForParameterScan(messages[index]);
        const rows = parseParameterRows(text);
        if (rows.length) {
          currentSelection.parameter_rows = filterParameterRows(rows);
          currentSelection.parameters = filterParameterMap(currentSelection.parameter_rows[0] || {});
          updateJiavuActions(currentSelection);
          return;
        }
        const values = filterParameterMap(extractParameterValues(text));
        if (Object.keys(values).length) {
          const currentRows = activeParameterRows();
          currentRows[0] = Object.assign({}, currentRows[0] || {}, values);
          currentSelection.parameter_rows = currentRows;
          currentSelection.parameters = filterParameterMap(currentRows[0] || {});
          updateJiavuActions(currentSelection);
          return;
        }
      }
    }

    // override?: { parameters: {name:value}, parameter_order: [name, ...], selection: {...} }
    //   富卡片提交时传 override，只发可见参数（不再从对话历史抽 parameter_rows）；
    //   兜底卡片场景 override 缺省，走原对话抽取逻辑。
    function sendJiavuAction(action, override) {
      const activeSelection = override && override.selection
        ? Object.assign({}, currentSelection || {}, override.selection)
        : currentSelection;
      if (!activeSelection) {
        setStatus('请先确认一个模型。', true);
        return;
      }
      if (action === 'generateModel') {
        const settings = Object.assign({}, currentSettings, buildSettingsFromInputs());
        if (!settings.jiavu_model_excel_dir) {
          setStatus('请先在设置中填写“生成模型 Excel 目录”，再生成模型。', true);
          openSettings();
          jiavuModelExcelDirInput.focus();
          return;
        }
      }
      let parameters;
      let parameterRows;
      let parameterOrder;
      if (override && override.parameters) {
        parameters = {};
        Object.entries(override.parameters).forEach(([k, v]) => {
          parameters[k] = v == null ? '' : String(v);
        });
        parameterOrder = Array.isArray(override.parameter_order)
          ? override.parameter_order.filter((k) => Object.prototype.hasOwnProperty.call(parameters, k))
          : Object.keys(parameters);
        parameterRows = [parameters];
      } else {
        refreshSelectionParametersFromConversation();
        parameterRows = activeParameterRows().map((row) => {
          const normalized = {};
          Object.entries(row || {}).forEach(([key, value]) => {
            if (!isIgnoredParameterName(key)) normalized[key] = value == null ? '' : String(value);
          });
          return normalized;
        });
        parameters = filterParameterMap(Object.assign({}, parameterRows[0] || {}, currentSelection.parameters || {}));
        Object.entries(parameters).forEach(([key, value]) => {
          parameters[key] = value == null ? '' : String(value);
        });
        parameterOrder = [];
        parameterRows.forEach((row) => {
          Object.keys(row || {}).forEach((key) => {
            if (!parameterOrder.includes(key)) parameterOrder.push(key);
          });
        });
        Object.keys(parameters).forEach((key) => {
          if (!parameterOrder.includes(key)) parameterOrder.push(key);
        });
      }
      const parameterBridgePayload = (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge)
          ? ciduParameterBridge.payload()
        : {
            parameter_bridge_version: '',
            calculation_snapshot_revision: '0',
            calculation_snapshot_id: '',
            parameter_extraction_rows: [],
            equipment_result_rows: []
          };
      const manualParameterOverrides = {};
      if (override && override.manual_parameter_overrides) {
        Object.entries(override.manual_parameter_overrides).forEach(([key, value]) => {
          if (key) manualParameterOverrides[key] = value == null ? '' : String(value);
        });
      }
      sendHost({
        type: 'jiavuAction',
        action,
        selected_model: activeSelection.selected_model || '',
        template_name: activeSelection.template_name || '',
        selected_full_node_path: activeSelection.selected_full_node_path || '',
        full_node_path: activeSelection.selected_full_node_path || '',
        tree_path: activeSelection.selected_full_node_path || '',
        product_id: numericProductId(activeSelection.product_id),
        spec_model_original: activeSelection.spec_model_original || activeSelection.spec_model || '',
        spec_model: activeSelection.spec_model || activeSelection.spec_model_original || '',
        material_code_prefix: activeSelection.material_code_prefix || '',
        parameters,
        parameter_rows: parameterRows,
        parameter_order: parameterOrder,
        manual_parameter_overrides: manualParameterOverrides,
        manifest_params: override && override.manifest_params ? override.manifest_params : [],
        parameter_bridge_version: parameterBridgePayload.parameter_bridge_version,
        calculation_snapshot_revision: parameterBridgePayload.calculation_snapshot_revision || '0',
        calculation_snapshot_id: parameterBridgePayload.calculation_snapshot_id || '',
        parameter_extraction_revision: parameterBridgePayload.parameter_extraction_revision || '0',
        equipment_result_revision: parameterBridgePayload.equipment_result_revision || '0',
        parameter_extraction_rows: parameterBridgePayload.parameter_extraction_rows,
        equipment_result_rows: parameterBridgePayload.equipment_result_rows
      });
    }

    function renderResult(result) {
      ensureChat();
      const wrap = document.createElement('article');
      wrap.className = 'message assistant';
      const roleEl = document.createElement('div');
      roleEl.className = 'role';
      roleEl.textContent = 'Cidu';
      const bubble = document.createElement('div');
      bubble.className = 'bubble markdown';
      if (result.answer_markdown) {
        setMarkdownHtml(bubble, stripInlineParameterSection(result.answer_markdown));
        appendParameterRequirementDetails(bubble, result);
        enrichParameterRequirementsFromJyasmService(bubble, result);
        wrap.append(roleEl, bubble);
        chat.appendChild(wrap);
        chat.scrollTop = chat.scrollHeight;
        updateJiavuActions(normalizeResultSelection(result));
        rememberRequestedParameters(result);
        return;
      }
      const answer = document.createElement('div');
      setMarkdownHtml(answer, result.message || '没有返回消息。');

      const summary = document.createElement('div');
      summary.className = 'summary-row';
      const model = compactText(result.selected_model || result.template_name, '暂无选型模型');
      const description = compactText(result.model_description, '未返回模型说明');
      const req = result.parameter_requirements || {};
      const numeric = result.numeric || {};
      const text = result.text || {};
      summary.innerHTML = [
        `<div><strong>选型：</strong>${escapeHtml(model)}</div>`,
        `<div><strong>说明：</strong>${escapeHtml(description)}</div>`,
        `<div><strong>参数：</strong>${Object.keys(req).length || Object.keys(numeric).length || Object.keys(text).length} 项</div>`
      ].join('');

      const details = document.createElement('details');
      details.className = 'details';
      const detailsTitle = document.createElement('summary');
      detailsTitle.textContent = '查看选型详情';
      const cards = document.createElement('div');
      cards.className = 'details-body';

      const modelRows = [];
      if (result.selected_model) modelRows.push(`名称：${result.selected_model}`);
      if (result.model_description) modelRows.push(`说明：${result.model_description}`);
      if (result.template_name && result.template_name !== result.selected_model) {
        modelRows.push(`参数化模板：${result.template_name}`);
      }
      appendCard(cards, '选型模型', modelRows.length ? modelRows : ['暂无选型模型']);

      const reqRows = parameterRequirementRows(result);
      appendCard(cards, '模型需要的参数', reqRows.length ? reqRows : ['未返回参数需求说明']);

      const numericRows = Object.keys(numeric).map((key) => `${key}：${numeric[key]}`);
      appendCard(cards, '已识别的数值参数', numericRows.length ? numericRows : ['暂无数值参数']);

      const textRows = Object.keys(text).map((key) => `${key}：${text[key]}`);
      appendCard(cards, '已识别的文本参数', textRows.length ? textRows : ['暂无文本参数']);

      if (result.created_file) {
        appendCard(cards, '建模文件', [result.created_file]);
      }

      details.append(detailsTitle, cards);
      bubble.append(answer, summary, details);
      wrap.append(roleEl, bubble);
      chat.appendChild(wrap);
      chat.scrollTop = chat.scrollHeight;
      updateJiavuActions(normalizeResultSelection(result));
      rememberRequestedParameters(result);
    }

    function renderAttachments(files) {
      currentAttachments = Array.isArray(files) ? files.slice() : [];
      attachmentsEl.innerHTML = '';
      currentAttachments.forEach((file) => {
        const chip = document.createElement('span');
        chip.className = 'chip';
        chip.textContent = file.name || file.path || '附件';
        attachmentsEl.appendChild(chip);
      });
    }

    // 把字节数格式化成 "1.60 MB" 风格 —— C++ 现在还没发 size 字段（仅 name + path），
    // 这里做向前兼容：拿不到就返回空串，前端只显示文件后缀。
    function formatFileSize(bytes) {
      const n = Number(bytes);
      if (!isFinite(n) || n <= 0) return '';
      if (n < 1024) return n + ' B';
      if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
      if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(2) + ' MB';
      return (n / 1024 / 1024 / 1024).toFixed(2) + ' GB';
    }

    function buildBubbleAttachment(file) {
      const card = document.createElement('div');
      card.className = 'bubble-attachment';
      if (file && file.path) card.title = String(file.path);
      const icon = document.createElement('span');
      icon.className = 'bubble-attachment-icon';
      // 文件 SVG —— 与"查看图"按钮同款 currentColor，避免 WebView2 emoji 渲染成方框
      icon.innerHTML = '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>';
      card.appendChild(icon);
      const body = document.createElement('div');
      body.className = 'bubble-attachment-body';
      const nameEl = document.createElement('div');
      nameEl.className = 'bubble-attachment-name';
      nameEl.textContent = String((file && (file.name || file.path)) || '附件');
      body.appendChild(nameEl);
      const rawName = String((file && file.name) || '');
      const dotIdx = rawName.lastIndexOf('.');
      const ext = (dotIdx >= 0 && dotIdx < rawName.length - 1) ? rawName.slice(dotIdx + 1).toUpperCase() : '';
      const sizeText = formatFileSize(file && file.size);
      const metaParts = [];
      if (ext) metaParts.push(ext);
      if (sizeText) metaParts.push(sizeText);
      if (metaParts.length) {
        const meta = document.createElement('div');
        meta.className = 'bubble-attachment-meta';
        meta.textContent = metaParts.join(' · ');
        body.appendChild(meta);
      }
      card.appendChild(body);
      return card;
    }

