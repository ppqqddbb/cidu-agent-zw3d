    // 用 parameter_requirements_struct（jyasm 结构化数据）渲染单条富参数卡片。
    // visibility==="hidden" 的参数已经在 JiavuKnowledgeSync 阶段被剔除，这里只兜底过滤一次；
    // visibility==="conditional" 的参数照样渲染，但右上角加 "条件显示" 标签，hover 显示原始 type_expr。
    function renderParameterCard(name, info) {
      const meta = info && typeof info === 'object' ? info : {};
      const card = document.createElement('div');
      card.className = 'param-card';
      const imageUrl = String(meta.image_url || '').trim();
      if (imageUrl) {
        const thumb = document.createElement('img');
        thumb.className = 'param-card-thumb';
        thumb.src = imageUrl;
        thumb.alt = String(meta.image || name || '');
        thumb.loading = 'lazy';
        thumb.onerror = () => { thumb.style.display = 'none'; card.classList.add('param-card-no-thumb'); };
        card.appendChild(thumb);
      } else {
        card.classList.add('param-card-no-thumb');
      }
      const metaBox = document.createElement('div');
      metaBox.className = 'param-card-meta';
      const nameEl = document.createElement('div');
      nameEl.className = 'param-card-name';
      nameEl.textContent = String(name || '');
      const unit = String(meta.unit || '').trim();
      if (unit) {
        const unitEl = document.createElement('span');
        unitEl.className = 'param-unit';
        unitEl.textContent = '[' + unit + ']';
        nameEl.appendChild(unitEl);
      }
      metaBox.appendChild(nameEl);
      const addRow = (label, value) => {
        const text = String(value || '').trim();
        if (!text) return;
        const row = document.createElement('div');
        row.className = 'param-card-row';
        const labelEl = document.createElement('span');
        labelEl.className = 'param-label';
        labelEl.textContent = label + '：';
        const valueEl = document.createElement('span');
        valueEl.textContent = text;
        row.append(labelEl, valueEl);
        metaBox.appendChild(row);
      };
      addRow('限制', meta.range);
      addRow('说明', meta.remark);
      addRow('默认', meta.default);
      card.appendChild(metaBox);
      if (String(meta.visibility || '').toLowerCase() === 'conditional') {
        const tag = document.createElement('span');
        tag.className = 'param-tag param-tag-conditional';
        tag.textContent = '条件显示';
        const typeExpr = String(meta.type_expr || '').trim();
        if (typeExpr) tag.title = '触发条件：' + typeExpr;
        card.appendChild(tag);
      }
      return card;
    }

    function appendParameterRequirementDetails(container, result) {
      if (!container) return;
      const safeResult = result || {};
      // 仅在"模型选型确认"响应里渲染参数说明卡片：技术协议处理链路的响应（参数提取/
      // 公式计算 Skill / 修改参数回显等）也可能携带 parameter_requirements 字段，但
      // 那些值是占位 null，渲染出来就是用户截图里的"模型需要的参数：全部 null"噪音。
      if (!isSelectionResponse(safeResult)) return;
      // 多模型结果由 enrichParameterRequirementsFromJyasmService 拆成多个独立交互表，
      // 这里不再渲染单一兜底卡片，避免多个模型参数混在一起。
      if (hasMultipleSelectionBlocks(safeResult.answer_markdown || '')) return;
      // 优先用 parameter_requirements_struct 渲染富卡片；缺失时退回旧的纯文本列表。
      const struct = safeResult.parameter_requirements_struct
        || (safeResult.answer_markdown
            ? (parseSelectionMetaFromMarkdown(safeResult.answer_markdown).parameter_requirements_struct || null)
            : null);
      const structEntries = struct && typeof struct === 'object'
        ? Object.entries(struct).filter(([key, info]) => {
            if (!key || isIgnoredParameterName(key)) return false;
            const vis = info && typeof info === 'object' ? String(info.visibility || '').toLowerCase() : '';
            return vis !== 'hidden';
          })
        : [];
      const reqRows = structEntries.length ? [] : parameterRequirementRows(safeResult);
      if (!structEntries.length && !reqRows.length) return;
      const previous = container.querySelector('[data-parameter-requirements="true"]');
      if (previous) previous.remove();
      const details = document.createElement('details');
      details.className = 'details';
      details.open = true;
      details.dataset.parameterRequirements = 'true';
      const detailsTitle = document.createElement('summary');
      detailsTitle.textContent = '参数说明';
      const cards = document.createElement('div');
      cards.className = 'details-body';
      if (structEntries.length) {
        const card = document.createElement('section');
        card.className = 'card';
        const heading = document.createElement('h3');
        heading.textContent = '模型需要的参数';
        const list = document.createElement('div');
        list.className = 'param-card-list';
        structEntries.forEach(([key, info]) => list.appendChild(renderParameterCard(key, info)));
        card.append(heading, list);
        cards.appendChild(card);
      } else {
        appendCard(cards, '模型需要的参数', reqRows);
      }
      details.append(detailsTitle, cards);
      container.appendChild(details);
    }

    function replaceStreamWithAnswer(markdown, result = {}) {
      const answer = String(markdown || '').trim();
      if (!answer || !activeStreamBubble) {
        return false;
      }
      setMarkdownHtml(activeStreamBubble, stripInlineParameterSection(answer));
      appendParameterRequirementDetails(activeStreamBubble, result);
      enrichParameterRequirementsFromJyasmService(activeStreamBubble, result);
      saveMessage({ role: 'assistant', text: answer });
      lastStreamHadAnswer = true;
      activeStreamBubble = null;
      activeStreamText = '';
      chat.scrollTop = chat.scrollHeight;
      return true;
    }

    function endStream() {
      if (activeStreamBubble) {
        if (activeStreamText.trim()) {
          const streamResult = { answer_markdown: activeStreamText };
          setMarkdownHtml(activeStreamBubble, stripInlineParameterSection(activeStreamText));
          appendParameterRequirementDetails(activeStreamBubble, streamResult);
          enrichParameterRequirementsFromJyasmService(activeStreamBubble, streamResult);
          updateJiavuActions(normalizeResultSelection(streamResult));
          rememberRequestedParameters(streamResult);
          lastStreamHadAnswer = true;
        } else {
          activeStreamBubble.parentElement.remove();
          lastStreamHadAnswer = false;
          activeStreamBubble = null;
        }
      }
    }

    function appendCard(container, title, rows) {
      const card = document.createElement('section');
      card.className = 'card';
      const heading = document.createElement('h3');
      heading.textContent = title;
      const list = document.createElement('ul');
      list.className = 'list';
      rows.forEach((row) => {
        const item = document.createElement('li');
        item.textContent = row;
        list.appendChild(item);
      });
      card.append(heading, list);
      container.appendChild(card);
    }

    function compactText(value, fallback) {
      return value && String(value).trim() ? String(value).trim() : fallback;
    }

    function stripMarkdown(value) {
      return String(value || '').replace(/\*\*/g, '').replace(/`/g, '').trim();
    }

    function normalizeParamKey(value) {
      return String(value || '')
        .replace(/\[[^\]]*\]/g, '')
        .replace(/【[^】]*】/g, '')
        .replace(/[（(][^）)]*[）)]/g, '')
        .replace(/[:：=，,。；;\s_\-]/g, '')
        .toLowerCase();
    }

    function isIgnoredParameterName(name) {
      const normalized = normalizeParamKey(name);
      return [
        '关键词',
        '关键字',
        '检索关键词',
        '检索关键字',
        'keyword',
        'keywords'
      ].includes(normalized);
    }

    function filterParameterMap(parameters) {
      const filtered = {};
      Object.entries(parameters || {}).forEach(([key, value]) => {
        if (!isIgnoredParameterName(key)) filtered[key] = value;
      });
      return filtered;
    }

    function filterParameterRows(rows) {
      return (rows || []).map((row) => filterParameterMap(row || {}));
    }

    function parameterNameAliases(name) {
      const raw = stripMarkdown(name);
      const aliases = new Set([raw]);
      const noUnit = raw
        .replace(/\[[^\]]*\]/g, '')
        .replace(/【[^】]*】/g, '')
        .replace(/[（(][^）)]*[）)]/g, '')
        .trim();
      if (noUnit) aliases.add(noUnit);
      const compact = noUnit.replace(/\s+/g, '');
      if (compact) aliases.add(compact);
      const shortName = compact.replace(/^(?:设计|模型|参数|设备|产品|部件|组件)/, '');
      if (shortName) aliases.add(shortName);
      return [...aliases].filter(Boolean).sort((a, b) => b.length - a.length);
    }

    function parameterValuePattern(alias) {
      const escaped = alias.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      return new RegExp(`${escaped}\\s*(?:[：:=为是])?\\s*([^\\s，,。；;、\\n\\r]+)`, 'i');
    }

    function findKnownParameterName(rawKey, knownParameters) {
      const normalized = normalizeParamKey(rawKey);
      return knownParameters.find((name) => normalizeParamKey(name) === normalized) || '';
    }

    function activeParameterRows() {
      const rows = Array.isArray(currentSelection && currentSelection.parameter_rows)
        ? currentSelection.parameter_rows
        : [];
      if (rows.length) {
        return filterParameterRows(rows);
      }
      return [filterParameterMap(currentSelection && currentSelection.parameters || {})];
    }

    function parseMarkdownParameters(markdown) {
      const values = {};
      const templateKeys = new Set();
      const fencePattern = /```(?:text|txt|plain)?\s*([\s\S]*?)```/gi;
      let fence;
      while ((fence = fencePattern.exec(String(markdown || ''))) !== null) {
        String(fence[1] || '').split(/\r?\n/).forEach((line) => {
          const match = line.match(/^\s*([^=：:\n\r]{1,48})\s*[=：:]\s*(.+?)\s*$/);
          if (!match) return;
          const key = stripMarkdown(match[1]);
          const value = stripMarkdown(match[2]).trim();
          if (key && value && value !== '待填写' && !isIgnoredParameterName(key)) {
            values[key] = value;
            templateKeys.add(normalizeParamKey(key));
          }
        });
      }
      const pattern = /^[-*]\s+(?:\*\*)?([^*：:\n\r]+?)(?:\*\*)?\s*[：:]\s*([^\n\r]+)/gm;
      let match;
      while ((match = pattern.exec(String(markdown || ''))) !== null) {
        const key = stripMarkdown(match[1]);
        const value = stripMarkdown(match[2]).replace(/^待用户提供$/, '').trim();
        if (templateKeys.has(normalizeParamKey(key))) continue;
        if (key && value && !isIgnoredParameterName(key)) {
          values[key] = value;
        }
      }
      return values;
    }

    function parseParameterRowsFromTable(text) {
      const lines = String(text || '').replace(/\r\n/g, '\n').split('\n');
      for (let index = 0; index < lines.length - 2; index += 1) {
        const headers = splitMarkdownTableLine(lines[index]);
        if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
        const rows = [];
        let rowIndex = index + 2;
        while (rowIndex < lines.length) {
          const cells = splitMarkdownTableLine(lines[rowIndex]);
          if (!cells.length) break;
          const row = {};
          headers.forEach((name, cellIndex) => {
            const key = stripMarkdown(name);
            if (key && !isIgnoredParameterName(key)) row[key] = stripMarkdown(cells[cellIndex] || '');
          });
          if (Object.values(row).some(Boolean)) rows.push(row);
          rowIndex += 1;
        }
        if (rows.length) return rows;
      }
      return [];
    }

    function parseParameterRowsFromDelimitedText(text) {
      const lines = String(text || '')
        .replace(/\r\n/g, '\n')
        .split('\n')
        .map((line) => line.trim())
        .filter(Boolean);
      if (lines.length < 2) return [];
      const delimiter = lines[0].includes('\t')
        ? '\t'
        : /[,，]/.test(lines[0])
          ? /[,，]/
          : null;
      if (!delimiter) return [];
      const headers = lines[0].split(delimiter).map(stripMarkdown).filter(Boolean);
      if (headers.length < 2) return [];
      const rows = [];
      for (const line of lines.slice(1)) {
        const cells = line.split(delimiter).map(stripMarkdown);
        const row = {};
        headers.forEach((name, index) => {
          if (!isIgnoredParameterName(name)) row[name] = cells[index] || '';
        });
        if (Object.values(row).some(Boolean)) rows.push(row);
      }
      return rows;
    }

    function parseParameterRows(text) {
      return parseParameterRowsFromTable(text).length
        ? parseParameterRowsFromTable(text)
        : parseParameterRowsFromDelimitedText(text);
    }

