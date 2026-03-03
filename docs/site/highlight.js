(() => {
  function escapeHtml(text) {
    return text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function highlight(text) {
    let escaped = escapeHtml(text);

    const rules = [
      { regex: /(&lt;%|%&gt;)/g, cls: "kw" },
      { regex: /(&lt;\/?[a-zA-Z0-9!\-]+?[^&]*?&gt;)/g, cls: "tag" },
      { regex: /(&quot;.*?&quot;|&#39;.*?&#39;)/g, cls: "str" },
      { regex: /\b(\d+(?:\.\d+)?)\b/g, cls: "num" },
      { regex: /(\$[a-zA-Z_][\w]*)/g, cls: "var" },
      {
        regex: /\b(var|function|for|in|end|if|elseif|else|echo|return|include|while|not|and|or)\b/g,
        cls: "kw",
      },
      { regex: /(\b[a-zA-Z_][\w]*)(?=\s*\()/g, cls: "fn" },
      { regex: /(\/\/.*?$)/gm, cls: "cmt" },
    ];

    for (const rule of rules) {
      escaped = escaped.replace(rule.regex, `<span class="${rule.cls}">$1</span>`);
    }
    return escaped;
  }

  document.addEventListener("DOMContentLoaded", () => {
    const blocks = document.querySelectorAll("pre code");
    blocks.forEach((code) => {
      if (code.innerHTML.includes("<span")) {
        return;
      }
      const text = code.textContent;
      code.innerHTML = highlight(text);
    });
  });
})();
