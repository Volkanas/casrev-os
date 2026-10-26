#!/usr/bin/env python3
"""
casrev-os internet arama koprusu.

Misafir isletim sistemi COM2 uzerinden satir tabanli sorgu gonderir; bu arac
host tarafinda DuckDuckGo HTML arayuzunu (yedek olarak Wikipedia API) kullanir.
Cerez, hesap veya API anahtari kullanmaz.

Protokol (sekme ile ayrilmis, UTF-8 yerine CP1254):
  Misafir -> host : Q\t<id>\t<query>\n
  Host -> misafir : R\t<id>\t<count>\n
                    T\t<title>\t<site>\t<url>\t<snippet>\n  (count kez)
                    END\t<id>\n
  Hata            : E\t<id>\t<message>\n
"""

import argparse
import atexit
import base64
import html
import io
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unicodedata
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0 Safari/537.36"
)

MAX_RESULTS = 2
MAX_TITLE = 76
MAX_SITE = 36
MAX_URL = 156
MAX_SNIPPET = 136

DDG_TIMEOUT = 4.0
WIKI_TIMEOUT = 2.5
QUERY_BUDGET = 7.0

PAGE_MAX_BYTES = 640 * 1024
PAGE_FETCH_TIMEOUT = 8.0
PAGE_MAX_LINES = 420
PAGE_LINE_WIDTH = 80
PAGE_MAX_BLOCKS = 220

# Gercek tarayici gorunumu: misafir penceresinin icerik paneliyle ayni.
BROWSER_VIEW_W = 676
BROWSER_VIEW_H = 394
BROWSER_LOAD_TIMEOUT = 12.0
BROWSER_ACTION_TIMEOUT = 10.0
PAGE_IMAGE_MAX_BYTES = 220 * 1024
PAGE_IMAGE_SETTLE = 0.55

ROOT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT_DIR / "build"
BROWSER_PID_FILE = BUILD_DIR / "casrev-browser.pid"


def log(message):
    print(f"[search-agent] {message}", flush=True)


class DuckDuckGoParser(HTMLParser):
    """DuckDuckGo HTML sonuclarindan baslik, baglanti ve ozet toplar."""

    VOID = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr",
    }

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.results = []
        self.mode = None
        self.depth = 0
        self.buffer = []
        self.href = ""

    def handle_starttag(self, tag, attrs):
        if self.mode:
            if tag not in self.VOID:
                self.depth += 1
            return

        attributes = dict(attrs)
        classes = (attributes.get("class") or "").split()

        if tag == "a" and "result__a" in classes:
            self.mode = "title"
            self.depth = 1
            self.buffer = []
            self.href = attributes.get("href") or ""
        elif self.results and not self.results[-1]["snippet"] and \
                "result__snippet" in classes:
            self.mode = "snippet"
            self.depth = 1
            self.buffer = []

    def handle_startendtag(self, tag, attrs):
        pass

    def handle_endtag(self, tag):
        if not self.mode or tag in self.VOID:
            return

        self.depth -= 1
        if self.depth > 0:
            return

        text = "".join(self.buffer).strip()
        if self.mode == "title":
            self.results.append({
                "title": text,
                "href": self.href,
                "snippet": "",
            })
        elif self.mode == "snippet" and self.results:
            self.results[-1]["snippet"] = text

        self.mode = None
        self.depth = 0
        self.buffer = []

    def handle_data(self, data):
        if self.mode:
            self.buffer.append(data)


def http_get(url, timeout):
    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8",
        "Accept-Language": "tr-TR,tr;q=0.9,en;q=0.8",
        "Cache-Control": "no-cache",
    })
    with urllib.request.urlopen(request, timeout=max(0.5, timeout)) as response:
        if response.status != 200:
            return ""
        return response.read(384 * 1024).decode("utf-8", "replace")


def site_from_url(url):
    try:
        host = urllib.parse.urlsplit(url).hostname or ""
    except ValueError:
        return "web"
    if host.startswith("www."):
        host = host[4:]
    return host or "web"


def resolve_ddg_link(href):
    if not href:
        return ""

    if href.startswith("//"):
        href = "https:" + href
    elif href.startswith("/"):
        href = "https://duckduckgo.com" + href

    try:
        parts = urllib.parse.urlsplit(href)
    except ValueError:
        return ""

    if parts.hostname and parts.hostname.endswith("duckduckgo.com"):
        query = urllib.parse.parse_qs(parts.query)
        target = query.get("uddg", [""])[0]
        if target:
            return target
        return ""

    if parts.scheme not in ("http", "https") or not parts.hostname:
        return ""
    return href


def search_duckduckgo(query, timeout):
    url = "https://html.duckduckgo.com/html/?q=" + urllib.parse.quote_plus(query)
    page = http_get(url, timeout)
    if not page:
        return []

    parser = DuckDuckGoParser()
    parser.feed(page)

    results = []
    seen = set()
    for item in parser.results:
        link = resolve_ddg_link(item["href"])
        title = re.sub(r"\s+", " ", html.unescape(item["title"])).strip()
        snippet = re.sub(r"\s+", " ", html.unescape(item["snippet"])).strip()
        if not link or not title or link in seen:
            continue
        seen.add(link)
        results.append({
            "title": title,
            "site": site_from_url(link),
            "url": link,
            "snippet": snippet or title,
        })
    return results


def strip_html(text):
    text = re.sub(r"<[^>]+>", " ", text or "")
    return re.sub(r"\s+", " ", html.unescape(text)).strip()


def search_wikipedia(query, language, timeout):
    params = urllib.parse.urlencode({
        "action": "query",
        "list": "search",
        "srsearch": query,
        "format": "json",
        "utf8": "1",
        "srlimit": "3",
    })
    url = f"https://{language}.wikipedia.org/w/api.php?{params}"
    payload = http_get(url, timeout)
    if not payload:
        return []

    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        return []

    results = []
    for item in data.get("query", {}).get("search", []):
        title = item.get("title", "").strip()
        if not title:
            continue
        page_url = (
            f"https://{language}.wikipedia.org/wiki/"
            + urllib.parse.quote(title.replace(" ", "_"), safe="")
        )
        results.append({
            "title": title,
            "site": f"{language}.wikipedia.org",
            "url": page_url,
            "snippet": strip_html(item.get("snippet", "")) or title,
        })
    return results


def normalize_space(text):
    return re.sub(r"\s+", " ", text or "").strip()


class ReadablePageParser(HTMLParser):
    """HTML'den okunabilir baslik, paragraf, madde ve alinti toplar."""

    VOID_TAGS = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr",
    }
    SKIP_TAGS = {
        "script", "style", "noscript", "template", "svg", "canvas",
        "nav", "header", "footer", "aside", "form", "button", "select",
        "textarea", "iframe", "video", "audio", "dialog",
    }
    SKIP_CLASSES = {
        "infobox", "navbox", "sidebar", "toc", "catlinks", "mw-editsection",
        "hatnote", "metadata", "ambox", "mbox", "reflist", "references",
        "mw-references-wrap", "reference", "noprint", "shortdescription",
        "printfooter", "flaggedrevs-color-1",
    }
    BLOCK_KINDS = {
        "p": 0, "pre": 0, "figcaption": 0,
        "h1": 1, "h2": 1, "h3": 1, "h4": 1, "h5": 1, "h6": 1,
        "li": 2, "blockquote": 3,
    }
    MAIN_IDS = {
        "mw-content-text", "content", "main", "main-content", "article",
        "primary", "bodycontent", "page-content",
    }
    MAIN_CLASSES = {
        "mw-parser-output", "article-body", "article-content", "post-content",
        "entry-content", "page-content", "markdown-body", "content-body",
        "story-body", "post-body",
    }

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.title_parts = []
        self.meta_title = ""
        self.description = ""
        self.in_title = False
        self.skip_depth = 0
        self.tag_stack = []
        self.main_depth = 0
        self.captures = []
        self.blocks = []
        self.fallback = []
        self.seen = set()

    def _is_main(self, tag, attrs):
        if tag in ("article", "main"):
            return True
        if (attrs.get("role") or "").lower() == "main":
            return True
        ident = (attrs.get("id") or "").lower()
        classes = (attrs.get("class") or "").lower().split()
        if ident in self.MAIN_IDS:
            return True
        return any(name in self.MAIN_CLASSES for name in classes)

    def _handle_meta(self, attrs):
        name = (attrs.get("name") or attrs.get("property") or "").lower()
        content = normalize_space(attrs.get("content") or "")
        if not content:
            return
        if name in ("description", "og:description") and not self.description:
            self.description = content
        elif name in ("og:title", "twitter:title") and not self.meta_title:
            self.meta_title = content

    def _flush(self, capture):
        text = normalize_space("".join(capture["parts"]))
        if not text:
            return
        kind = capture["kind"]
        minimum = 2 if kind == 1 else 3 if kind == 2 else 8 if kind == 3 else 12
        if len(text) < minimum or text in self.seen:
            return
        self.seen.add(text)
        if len(self.blocks) < PAGE_MAX_BLOCKS:
            self.blocks.append({
                "kind": kind,
                "text": text,
                "main": capture["main"],
            })

    def _open_block(self, tag):
        kind = self.BLOCK_KINDS.get(tag)
        if kind is None or self.skip_depth:
            return
        self.captures.append({
            "tag": tag,
            "kind": kind,
            "main": self.main_depth > 0,
            "parts": [],
        })

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        attributes = dict(attrs)

        if tag == "title":
            self.in_title = True
            self.title_parts = []
            return
        if self.in_title:
            return
        if self.skip_depth:
            if tag not in self.VOID_TAGS:
                self.skip_depth += 1
            return
        classes = (attributes.get("class") or "").lower().split()
        if tag in self.SKIP_TAGS or any(name in self.SKIP_CLASSES
                                        for name in classes):
            if tag not in self.VOID_TAGS:
                self.skip_depth = 1
            return
        if tag == "meta":
            self._handle_meta(attributes)
            return
        if tag == "br":
            if self.captures:
                self.captures[-1]["parts"].append(" ")
            return

        is_main = self._is_main(tag, attributes)
        if tag not in self.VOID_TAGS:
            self.tag_stack.append((tag, is_main))
        if is_main:
            self.main_depth += 1

        self._open_block(tag)

    def handle_startendtag(self, tag, attrs):
        tag = tag.lower()
        if self.skip_depth:
            return
        if tag == "meta":
            self._handle_meta(dict(attrs))
        elif tag == "br" and self.captures:
            self.captures[-1]["parts"].append(" ")

    def handle_data(self, data):
        if self.in_title:
            self.title_parts.append(data)
            return
        if self.skip_depth:
            return
        self.fallback.append(data)
        if self.captures:
            self.captures[-1]["parts"].append(data)

    def handle_endtag(self, tag):
        tag = tag.lower()

        if tag == "title":
            if self.in_title:
                self.in_title = False
            return
        if self.in_title:
            return

        if self.skip_depth:
            if tag not in self.VOID_TAGS:
                self.skip_depth -= 1
            return

        for idx in range(len(self.captures) - 1, -1, -1):
            if self.captures[idx]["tag"] == tag:
                while len(self.captures) > idx:
                    self._flush(self.captures.pop())
                break

        while self.tag_stack:
            opened, is_main = self.tag_stack.pop()
            if is_main and self.main_depth > 0:
                self.main_depth -= 1
            if opened == tag:
                break

    def close(self):
        super().close()
        while self.captures:
            self._flush(self.captures.pop())


def wrap_page_text(text, width):
    words = text.split()
    lines = []
    current = ""

    for word in words:
        if len(word) > width:
            word = word[:max(1, width - 3)].rstrip() + "..."
        if not current:
            current = word
        elif len(current) + 1 + len(word) <= width:
            current += " " + word
        else:
            lines.append(current)
            current = word

    if current:
        lines.append(current)
    return lines


def build_page_lines(parser):
    blocks = parser.blocks
    main_blocks = [block for block in blocks if block["main"]]
    if main_blocks:
        blocks = main_blocks

    if not blocks:
        fallback = normalize_space(" ".join(parser.fallback))
        if fallback:
            while fallback and len(blocks) < 24:
                chunk = fallback[:300]
                cut = max(chunk.rfind(". "), chunk.rfind("? "), chunk.rfind("! "))
                if cut > 80:
                    chunk = chunk[:cut + 1]
                chunk = chunk.strip()
                if chunk:
                    blocks.append({"kind": 0, "text": chunk, "main": False})
                fallback = fallback[len(chunk):].strip()

    if not blocks and parser.description:
        blocks = [{"kind": 0, "text": parser.description, "main": False}]

    lines = []
    truncated = False

    def add(kind, text, prefix="", indent=""):
        nonlocal truncated
        wrapped = wrap_page_text(text, PAGE_LINE_WIDTH - len(prefix))
        for index, part in enumerate(wrapped):
            if len(lines) >= PAGE_MAX_LINES:
                truncated = True
                return
            lines.append((kind, (prefix if index == 0 else indent) + part))

    def blank():
        if lines and lines[-1][1]:
            lines.append((4, ""))

    previous = -1
    for block in blocks:
        kind = block["kind"]
        text = block["text"]
        if kind == 1:
            blank()
            add(1, text)
            blank()
        elif kind == 2:
            if previous != 2:
                blank()
            add(2, text, "- ", "  ")
        elif kind == 3:
            blank()
            add(3, text, "| ", "  ")
            blank()
        else:
            add(0, text)
            blank()
        previous = kind

    while lines and not lines[-1][1]:
        lines.pop()

    if truncated:
        note = "Icerigin devami bu surumde gosterilmiyor."
        if len(lines) >= PAGE_MAX_LINES:
            lines = lines[:PAGE_MAX_LINES - 1]
        lines.append((5, note))

    if not lines:
        lines.append((0, "Sayfa metni okunamadi."))
    return lines


def decode_page(raw, content_type):
    charset = None
    match = re.search(r"charset\s*=\s*[\"']?([A-Za-z0-9._-]+)",
                      content_type or "", re.I)
    if match:
        charset = match.group(1)
    if not charset:
        match = re.search(rb"charset\s*=\s*[\"']?([A-Za-z0-9._-]+)",
                          raw[:8192], re.I)
        if match:
            charset = match.group(1).decode("ascii", "ignore")

    for encoding in (charset, "utf-8", "cp1254", "iso-8859-9",
                     "windows-1252", "latin-1"):
        if not encoding:
            continue
        try:
            return raw.decode(encoding)
        except (LookupError, UnicodeDecodeError):
            continue
    return raw.decode("utf-8", "replace")


def fetch_page(url):
    parts = urllib.parse.urlsplit(url)
    if parts.scheme not in ("http", "https") or not parts.hostname:
        raise ValueError("Gecersiz sayfa adresi")

    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "text/html,application/xhtml+xml;q=0.9,*/*;q=0.5",
        "Accept-Language": "tr-TR,tr;q=0.9,en;q=0.8",
        "Cache-Control": "no-cache",
    })
    with urllib.request.urlopen(request, timeout=PAGE_FETCH_TIMEOUT) as response:
        if response.status != 200:
            raise ValueError(f"Sunucu {response.status} dondurdu")
        content_type = response.headers.get("Content-Type", "")
        lower_type = content_type.lower()
        if lower_type and not any(token in lower_type for token in
                                  ("text/", "html", "xml", "json")):
            raise ValueError("Bu adres metin sayfasi degil")
        raw = response.read(PAGE_MAX_BYTES + 1)

    truncated = len(raw) > PAGE_MAX_BYTES
    if truncated:
        raw = raw[:PAGE_MAX_BYTES]

    page = decode_page(raw, content_type)
    parser = ReadablePageParser()
    try:
        parser.feed(page)
        parser.close()
    except Exception:
        pass

    title = normalize_space("".join(parser.title_parts)) or parser.meta_title
    title = title or site_from_url(url)
    if truncated:
        parser.blocks.append({
            "kind": 5,
            "text": "Sayfa boyutu sinirlandi; icerigin bir bolumu alindi.",
            "main": False,
        })
    lines = build_page_lines(parser)
    return title, site_from_url(url), lines


def qoi_encode_rgb(width, height, pixels):
    """RGB888 tamponunu QOI'ye cevirir (misafirdeki cozucu ile ayni bicim)."""
    if width <= 0 or height <= 0 or len(pixels) < width * height * 3:
        raise ValueError("Gecersiz QOI goruntusu")

    out = bytearray(b"qoif")
    out += struct.pack(">II", width, height)
    out += b"\x03\x00"             # 3 kanal, sRGB

    index = [0] * 64
    prev_r = prev_g = prev_b = 0
    run = 0
    append = out.append

    for pos in range(0, width * height * 3, 3):
        r = pixels[pos]
        g = pixels[pos + 1]
        b = pixels[pos + 2]

        if r == prev_r and g == prev_g and b == prev_b:
            run += 1
            if run == 62:
                append(0xC0 | (run - 1))
                run = 0
            continue

        if run:
            append(0xC0 | (run - 1))
            run = 0

        slot = (r * 3 + g * 5 + b * 7 + 255 * 11) & 63
        packed = (r << 24) | (g << 16) | (b << 8) | 255
        if index[slot] == packed:
            append(slot)
        else:
            index[slot] = packed
            dr = r - prev_r
            dg = g - prev_g
            db = b - prev_b
            if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                append(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2))
            else:
                dr_dg = dr - dg
                db_dg = db - dg
                if -32 <= dg <= 31 and -8 <= dr_dg <= 7 and -8 <= db_dg <= 7:
                    append(0x80 | (dg + 32))
                    append(((dr_dg + 8) << 4) | (db_dg + 8))
                else:
                    append(0xFE)
                    append(r)
                    append(g)
                    append(b)

        prev_r, prev_g, prev_b = r, g, b

    if run:
        append(0xC0 | (run - 1))
    out += b"\x00\x00\x00\x00\x00\x00\x00\x01"
    return bytes(out)


class WebSocketClient:
    """CDP icin kucuk, bagimsiz WebSocket istemcisi (yalnizca localhost)."""

    def __init__(self, url, timeout=30.0):
        parts = urllib.parse.urlsplit(url)
        if parts.scheme != "ws" or not parts.hostname:
            raise ValueError("Gecersiz WebSocket adresi")

        self.sock = socket.create_connection((parts.hostname, parts.port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buffer = b""
        self.closed = False

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        path = parts.path or "/"
        if parts.query:
            path += "?" + parts.query
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {parts.hostname}:{parts.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            f"Origin: http://{parts.hostname}:{parts.port}\r\n"
            "\r\n"
        )
        self.sock.sendall(request.encode("ascii"))

        header = b""
        while b"\r\n\r\n" not in header:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("WebSocket el sikismasi kapandi")
            header += chunk
        head, self.buffer = header.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n")[0].decode("latin-1")
        if " 101 " not in status:
            raise ConnectionError("WebSocket el sikismasi basarisiz: " + status)

    def _read_exact(self, count):
        while len(self.buffer) < count:
            chunk = self.sock.recv(max(65536, count - len(self.buffer)))
            if not chunk:
                raise ConnectionError("WebSocket baglantisi kapandi")
            self.buffer += chunk
        data, self.buffer = self.buffer[:count], self.buffer[count:]
        return data

    def _send_frame(self, opcode, payload):
        mask = os.urandom(4)
        length = len(payload)
        frame = bytearray([0x80 | opcode])
        if length < 126:
            frame.append(0x80 | length)
        elif length < 65536:
            frame.append(0x80 | 126)
            frame += struct.pack("!H", length)
        else:
            frame.append(0x80 | 127)
            frame += struct.pack("!Q", length)
        frame += mask
        frame += bytes(value ^ mask[index & 3] for index, value in enumerate(payload))
        self.sock.sendall(frame)

    def send_text(self, text):
        self._send_frame(0x1, text.encode("utf-8"))

    def recv_text(self):
        message = b""
        while True:
            first, second = self._read_exact(2)
            fin = first & 0x80
            opcode = first & 0x0F
            masked = second & 0x80
            length = second & 0x7F
            if length == 126:
                length = struct.unpack("!H", self._read_exact(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self._read_exact(8))[0]
            if length > 16 * 1024 * 1024:
                raise ConnectionError("WebSocket cercevesi cok buyuk")

            mask = self._read_exact(4) if masked else None
            payload = self._read_exact(length)
            if mask:
                payload = bytes(value ^ mask[index & 3]
                                for index, value in enumerate(payload))

            if opcode == 0x9:          # ping
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:          # pong
                continue
            if opcode == 0x8:          # close
                self.closed = True
                raise ConnectionError("WebSocket kapatildi")

            message += payload
            if fin:
                return message.decode("utf-8", "replace")

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self._send_frame(0x8, b"")
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


def find_browser():
    """Edge, sonra Chrome arar; yoksa None dondurur."""
    candidates = []
    for env_name in ("ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"):
        base = os.environ.get(env_name)
        if not base:
            continue
        candidates += [
            os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"),
            os.path.join(base, "Google", "Chrome", "Application", "chrome.exe"),
        ]
    for name in ("msedge", "microsoft-edge", "chrome", "google-chrome"):
        found = shutil.which(name)
        if found:
            candidates.append(found)

    seen = set()
    for path in candidates:
        path = os.path.abspath(path)
        if path in seen:
            continue
        seen.add(path)
        if os.path.isfile(path):
            return path
    return None


class HeadlessBrowser:
    """Edge/Chrome CDP ile gercek CSS/JS/gorsel sayfa render eder."""

    def __init__(self):
        self.process = None
        self.socket = None
        self.cdp = None
        self.port = 0
        self.profile = None
        self.command_id = 0
        self.stderr_log = None

    @staticmethod
    def _free_port():
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.bind(("127.0.0.1", 0))
            return probe.getsockname()[1]
        finally:
            probe.close()

    def _command(self, method, params=None):
        if not self.socket:
            raise ConnectionError("Tarayici baglantisi yok")
        self.command_id += 1
        ident = self.command_id
        self.socket.send_text(json.dumps({
            "id": ident,
            "method": method,
            "params": params or {},
        }))
        while True:
            message = json.loads(self.socket.recv_text())
            if message.get("id") != ident:
                continue
            if "error" in message:
                raise RuntimeError(f"{method}: {message['error'].get('message', 'CDP hatasi')}")
            return message.get("result", {})

    def _evaluate(self, expression):
        result = self._command("Runtime.evaluate", {
            "expression": expression,
            "returnByValue": True,
            "awaitPromise": True,
        })
        if result.get("exceptionDetails"):
            raise RuntimeError("Sayfa JavaScript hatasi")
        value = result.get("result", {}).get("value")
        return value

    def _write_pid(self):
        try:
            BUILD_DIR.mkdir(parents=True, exist_ok=True)
            BROWSER_PID_FILE.write_text(str(self.process.pid), encoding="ascii")
        except OSError:
            pass

    @staticmethod
    def _kill_previous():
        try:
            pid = int(BROWSER_PID_FILE.read_text(encoding="ascii").strip())
        except (OSError, ValueError):
            return
        if pid > 0:
            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            BROWSER_PID_FILE.unlink()
        except OSError:
            pass

    def start(self):
        self.close()
        exe = find_browser()
        if not exe:
            raise RuntimeError("Edge veya Chrome bulunamadi")

        last_error = None
        for attempt in (1, 2):
            try:
                self._launch(exe)
                break
            except Exception as exc:
                last_error = exc
                log(f"tarayici baslatma denemesi {attempt} basarisiz: {exc}")
                self.close()
                if attempt == 1:
                    time.sleep(0.8)
        else:
            raise last_error

        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/json/new?about:blank",
            method="PUT",
        )
        try:
            with urllib.request.urlopen(request, timeout=5.0) as response:
                target = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError:
            request = urllib.request.Request(
                f"http://127.0.0.1:{self.port}/json/new?about:blank"
            )
            with urllib.request.urlopen(request, timeout=5.0) as response:
                target = json.loads(response.read().decode("utf-8"))

        self.socket = WebSocketClient(target["webSocketDebuggerUrl"], timeout=30.0)
        self._command("Page.enable")
        self._command("Runtime.enable")
        self._command("Emulation.setDeviceMetricsOverride", {
            "width": BROWSER_VIEW_W,
            "height": BROWSER_VIEW_H,
            "deviceScaleFactor": 1,
            "mobile": False,
            "screenWidth": BROWSER_VIEW_W,
            "screenHeight": BROWSER_VIEW_H,
        })
        log("tarayici hazir (CSS, JavaScript, gorseller etkin)")

    def _launch(self, exe):
        self._kill_previous()
        BUILD_DIR.mkdir(parents=True, exist_ok=True)
        self.port = self._free_port()
        self.profile = tempfile.mkdtemp(prefix="casrev-browser-")
        log(f"tarayici baslatiliyor: {os.path.basename(exe)} port {self.port}")

        args = [
            exe,
            "--headless=new",
            "--disable-gpu",
            "--hide-scrollbars",
            "--no-first-run",
            "--no-default-browser-check",
            "--disable-extensions",
            "--disable-background-timer-throttling",
            "--disable-renderer-backgrounding",
            "--mute-audio",
            f"--remote-debugging-port={self.port}",
            f"--user-data-dir={self.profile}",
            f"--window-size={BROWSER_VIEW_W},{BROWSER_VIEW_H}",
            "--force-device-scale-factor=1",
            "about:blank",
        ]
        creationflags = 0
        if os.name == "nt":
            creationflags = (getattr(subprocess, "CREATE_NO_WINDOW", 0) |
                             getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
        self.stderr_log = open(BUILD_DIR / "casrev-browser.log", "wb")
        self.process = subprocess.Popen(
            args,
            stdout=self.stderr_log,
            stderr=self.stderr_log,
            creationflags=creationflags,
        )
        self._write_pid()

        started = time.monotonic()
        deadline = started + 25.0
        version_url = f"http://127.0.0.1:{self.port}/json/version"
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"Tarayici erken kapandi (kod {self.process.returncode})")
            try:
                with urllib.request.urlopen(version_url, timeout=0.6) as response:
                    json.loads(response.read().decode("utf-8"))
                break
            except (OSError, ValueError):
                time.sleep(0.2)
        else:
            raise RuntimeError(
                f"Tarayici hata ayiklama portu acilmadi "
                f"({time.monotonic() - started:.1f} s)")
        log(f"tarayici CDP hazir ({time.monotonic() - started:.1f} s)")

    def ensure(self):
        if self.process is not None and self.process.poll() is None and self.socket:
            return
        self.start()

    def close(self):
        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None
        self.cdp = None
        if self.process is not None:
            pid = self.process.pid
            if self.process.poll() is None:
                if os.name == "nt":
                    subprocess.run(
                        ["taskkill", "/PID", str(pid), "/T", "/F"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                    )
                else:
                    self.process.terminate()
            try:
                self.process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None
        if self.stderr_log is not None:
            try:
                self.stderr_log.close()
            except OSError:
                pass
            self.stderr_log = None
        try:
            BROWSER_PID_FILE.unlink()
        except OSError:
            pass
        if self.profile:
            shutil.rmtree(self.profile, ignore_errors=True)
            self.profile = None

    def wait_ready(self, timeout=BROWSER_LOAD_TIMEOUT):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if self._evaluate("document.readyState") == "complete":
                    break
            except RuntimeError:
                pass
            time.sleep(0.25)
        time.sleep(PAGE_IMAGE_SETTLE)

    def navigate(self, url):
        self.ensure()
        result = self._command("Page.navigate", {"url": url})
        if result.get("errorText"):
            raise RuntimeError(result["errorText"])
        self.wait_ready()
        try:
            self._evaluate("window.scrollTo(0, 0)")
        except RuntimeError:
            pass
        info = self.info()
        if info["url"].startswith("chrome-error://"):
            raise RuntimeError("Sayfa yuklenemedi")
        return info

    def info(self):
        expression = (
            "JSON.stringify({"
            "title:document.title||'',"
            "url:location.href,"
            "scrollY:Math.round(window.scrollY||0),"
            "scrollH:Math.max(document.documentElement?document.documentElement.scrollHeight:0,"
            "document.body?document.body.scrollHeight:0),"
            "viewH:Math.round(window.innerHeight||0),"
            "ready:document.readyState"
            "})"
        )
        raw = self._evaluate(expression)
        if not raw:
            raise RuntimeError("Sayfa bilgisi okunamadi")
        info = json.loads(raw)

        can_back = False
        can_forward = False
        try:
            history = self._command("Page.getNavigationHistory")
            entries = history.get("entries", [])
            index = history.get("currentIndex", 0)
            if 0 < index < len(entries):
                previous = entries[index - 1].get("url", "")
                can_back = bool(previous) and not previous.startswith("about:")
            if 0 <= index + 1 < len(entries):
                following = entries[index + 1].get("url", "")
                can_forward = bool(following) and not following.startswith("about:")
        except RuntimeError:
            pass
        info["canBack"] = can_back
        info["canForward"] = can_forward
        return info

    def _capture_qoi(self):
        result = self._command("Page.captureScreenshot", {
            "format": "png",
            "fromSurface": True,
            "captureBeyondViewport": False,
        })
        raw_png = base64.b64decode(result.get("data", ""))
        if not raw_png:
            raise RuntimeError("Tarayici ekran goruntusu bos")

        try:
            from PIL import Image
        except ImportError as exc:
            raise RuntimeError("Pillow kurulu degil") from exc

        image = Image.open(io.BytesIO(raw_png)).convert("RGB")
        wanted = (BROWSER_VIEW_W, BROWSER_VIEW_H)
        if image.size != wanted:
            image = image.resize(wanted, Image.Resampling.LANCZOS)

        qoi = qoi_encode_rgb(image.width, image.height, image.tobytes())
        if len(qoi) <= PAGE_IMAGE_MAX_BYTES:
            return qoi

        log(f"gorsel {len(qoi)} bayt; renk paleti azaltiliyor")
        for colors in (192, 128, 96, 64):
            reduced = image.quantize(colors=colors,
                                     method=Image.Quantize.MEDIANCUT,
                                     dither=Image.Dither.NONE).convert("RGB")
            qoi = qoi_encode_rgb(reduced.width, reduced.height, reduced.tobytes())
            if len(qoi) <= PAGE_IMAGE_MAX_BYTES:
                return qoi
        raise RuntimeError("Sayfa gorseli aktarim sinirini asiyor")

    def snapshot(self):
        info = self.info()
        qoi = self._capture_qoi()
        return info, qoi

    def scroll(self, delta):
        self.ensure()
        try:
            before = self._evaluate("Math.round(window.scrollY||0)")
            self._evaluate(f"window.scrollBy(0,{int(delta)})")
            time.sleep(0.2)
            after = self._evaluate("Math.round(window.scrollY||0)")
            if abs(int(after) - int(before)) < 1 and delta:
                self._command("Input.dispatchMouseEvent", {
                    "type": "mouseWheel",
                    "x": BROWSER_VIEW_W // 2,
                    "y": BROWSER_VIEW_H // 2,
                    "deltaX": 0,
                    "deltaY": int(delta),
                })
        except RuntimeError:
            pass
        time.sleep(0.45)
        return self.snapshot()

    def scroll_to(self, position):
        self.ensure()
        self._evaluate(f"window.scrollTo(0,{max(0, int(position))})")
        time.sleep(0.45)
        return self.snapshot()

    def click(self, x, y):
        self.ensure()
        x = max(0, min(BROWSER_VIEW_W - 1, int(x)))
        y = max(0, min(BROWSER_VIEW_H - 1, int(y)))
        self._command("Input.dispatchMouseEvent", {
            "type": "mouseMoved", "x": x, "y": y,
            "button": "none", "buttons": 0,
        })
        self._command("Input.dispatchMouseEvent", {
            "type": "mousePressed", "x": x, "y": y,
            "button": "left", "buttons": 1, "clickCount": 1,
        })
        self._command("Input.dispatchMouseEvent", {
            "type": "mouseReleased", "x": x, "y": y,
            "button": "left", "buttons": 0, "clickCount": 1,
        })
        time.sleep(0.9)
        self.wait_ready(BROWSER_ACTION_TIMEOUT)
        return self.snapshot()

    def _history_step(self, direction):
        self.ensure()
        history = self._command("Page.getNavigationHistory")
        entries = history.get("entries", [])
        index = history.get("currentIndex", 0) + direction
        if 0 <= index < len(entries):
            self._command("Page.navigateToHistoryEntry", {
                "entryId": entries[index]["id"],
            })
            self.wait_ready(BROWSER_ACTION_TIMEOUT)
        return self.snapshot()

    def back(self):
        return self._history_step(-1)

    def forward(self):
        return self._history_step(1)

    def reload(self):
        self.ensure()
        self._command("Page.reload", {"ignoreCache": True})
        self.wait_ready(BROWSER_ACTION_TIMEOUT)
        return self.snapshot()


_browser = None


def browser_instance():
    global _browser
    if _browser is None:
        _browser = HeadlessBrowser()
        atexit.register(_browser.close)
    return _browser


def browser_shutdown():
    global _browser
    if _browser is not None:
        _browser.close()
        _browser = None


def send_visual(sock, page_id, info, qoi):
    title = normalize_space(info.get("title") or "") or site_from_url(info.get("url", ""))
    header = [
        "V",
        page_id,
        str(BROWSER_VIEW_W),
        str(BROWSER_VIEW_H),
        str(len(qoi)),
        to_cp1254(title, MAX_TITLE),
        to_cp1254(site_from_url(info.get("url", "")), MAX_SITE),
        sanitize_url(info.get("url", "")),
        str(int(info.get("scrollY", 0))),
        str(int(info.get("scrollH", 0))),
        str(int(info.get("viewH", BROWSER_VIEW_H))),
        "1" if info.get("canBack") else "0",
        "1" if info.get("canForward") else "0",
    ]
    send_line(sock, header)
    sock.sendall(qoi)
    log(f"sayfa {page_id}: gorsel {BROWSER_VIEW_W}x{BROWSER_VIEW_H}, "
        f"{len(qoi)} bayt, kaydirma {info.get('scrollY', 0)}/{info.get('scrollH', 0)}, "
        f"geri {1 if info.get('canBack') else 0}, "
        f"ileri {1 if info.get('canForward') else 0}, "
        f"{sanitize_url(info.get('url', ''))[:72]}")


def handle_visual_page(sock, page_id, url):
    url = (url or "").strip()
    log(f"gorsel sayfa {page_id}: {url[:96]}")
    if not url:
        send_line(sock, ["X", page_id, "Sayfa adresi bos"])
        return

    try:
        info = browser_instance().navigate(url)
        info, qoi = browser_instance().snapshot()
        send_visual(sock, page_id, info, qoi)
        return
    except Exception as exc:
        log(f"gorsel sayfa {page_id} basarisiz: {type(exc).__name__}: {exc}")

    # Gercek tarayici yoksa mevcut metin okuyucusuna dus.
    try:
        title, site, lines = fetch_page(url)
    except Exception as exc:
        log(f"metin yedegi de basarisiz: {type(exc).__name__}: {exc}")
        send_line(sock, ["X", page_id, to_cp1254("Sayfa acilamadi", 60)])
        return

    send_line(sock, [
        "G", page_id, str(len(lines)), to_cp1254(title, MAX_TITLE),
        to_cp1254(site, MAX_SITE), sanitize_url(url),
    ])
    for kind, line in lines:
        send_line(sock, ["B", page_id, str(kind), to_cp1254(line, 96)])
    send_line(sock, ["Z", page_id])
    log(f"sayfa {page_id}: metin yedegi, {len(lines)} satir gonderildi")


def handle_visual_action(sock, page_id, action, fields):
    browser = browser_instance()
    log(f"gorsel eylem {page_id}: {action} {fields}")
    try:
        browser.ensure()
        if action == "S":
            info, qoi = browser.scroll(int(fields[0]))
        elif action == "J":
            info, qoi = browser.scroll_to(int(fields[0]))
        elif action == "C":
            info, qoi = browser.click(int(fields[0]), int(fields[1]))
        elif action == "B":
            info, qoi = browser.back()
        elif action == "F":
            info, qoi = browser.forward()
        elif action == "Y":
            info, qoi = browser.reload()
        else:
            raise ValueError("Bilinmeyen eylem")
        send_visual(sock, page_id, info, qoi)
    except Exception as exc:
        log(f"gorsel eylem {page_id} basarisiz: {type(exc).__name__}: {exc}")
        send_line(sock, ["X", page_id, to_cp1254("Sayfa eylemi basarisiz", 60)])


def handle_page(sock, page_id, url):
    url = (url or "").strip()
    log(f"sayfa {page_id}: {url[:96]}")
    if not url:
        send_line(sock, ["X", page_id, "Sayfa adresi bos"])
        return

    try:
        title, site, lines = fetch_page(url)
    except Exception as exc:
        log(f"sayfa {page_id} basarisiz: {type(exc).__name__}: {exc}")
        send_line(sock, ["X", page_id, to_cp1254("Sayfa acilamadi", 60)])
        return

    send_line(sock, [
        "G", page_id, str(len(lines)), to_cp1254(title, MAX_TITLE),
        to_cp1254(site, MAX_SITE), sanitize_url(url),
    ])
    for kind, line in lines:
        send_line(sock, ["B", page_id, str(kind), to_cp1254(line, 96)])
    send_line(sock, ["Z", page_id])
    log(f"sayfa {page_id}: {len(lines)} satir gonderildi")


def search_web(query):
    """DuckDuckGo + Wikipedia; en fazla MAX_RESULTS benzersiz sonuc dondurur."""
    deadline = time.monotonic() + QUERY_BUDGET
    results = []

    def remaining():
        return deadline - time.monotonic()

    def add(items):
        for item in items:
            if len(results) >= MAX_RESULTS:
                return
            if any(existing["url"] == item["url"] for existing in results):
                continue
            results.append(item)

    try:
        add(search_duckduckgo(query, min(DDG_TIMEOUT, remaining())))
        log(f"duckduckgo: {len(results)} sonuc")
    except Exception as exc:
        log(f"duckduckgo hatasi: {type(exc).__name__}: {exc}")

    for language in ("tr", "en"):
        if len(results) >= MAX_RESULTS or remaining() < 1.0:
            break
        try:
            add(search_wikipedia(query, language, min(WIKI_TIMEOUT, remaining())))
            log(f"wikipedia/{language}: toplam {len(results)} sonuc")
        except Exception as exc:
            log(f"wikipedia/{language} hatasi: {type(exc).__name__}: {exc}")

    return results[:MAX_RESULTS]


def to_cp1254(text, limit):
    """Misafir arayuzunun tek baytlik CP1254 metnine sigdirir.

    Arayuz yalnizca ASCII ve 12 Turkce glifi ozel olarak cizer; diger
    aksanli harfler BIOS fontunda yanlis gorunebilecegi icin ASCII'ye
    katlanir.
    """
    punctuation = {
        "\u2014": "-", "\u2013": "-", "\u2212": "-",
        "\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"',
        "\u2026": "...", "\u00a0": " ", "\u2022": "-", "\u00b7": "-",
        "\u200b": "",
    }
    latin = {
        "\u00df": "ss", "\u00e6": "ae", "\u00c6": "AE",
        "\u00f8": "o", "\u00d8": "O", "\u0142": "l", "\u0141": "L",
        "\u0111": "d", "\u0110": "D", "\u00fe": "th", "\u00de": "Th",
        "\u00f0": "d", "\u00d0": "D",
    }
    symbols = {
        "\u00a9": "(c)", "\u00ae": "(r)", "\u2122": "TM",
        "\u00b0": "deg", "\u20ac": "EUR", "\u00a3": "GBP",
        "\u00a5": "JPY", "\u00d7": "x", "\u00f7": "/", "\u00b1": "+/-",
    }
    turkish = set("ÇçĞğİıÖöŞşÜü")
    output = []
    for char in (text or ""):
        char = punctuation.get(char, char)
        char = symbols.get(char, char)
        if not char:
            continue
        if ord(char) < 32 or ord(char) == 127:
            char = " "
        if char in turkish or " " <= char <= "~":
            output.append(char)
            continue
        if char in latin:
            output.append(latin[char])
            continue

        ascii_text = unicodedata.normalize("NFKD", char).encode(
            "ascii", "ignore"
        ).decode("ascii")
        output.append(ascii_text or "?")

    clean = re.sub(r"\s+", " ", "".join(output)).strip()
    if len(clean) > limit:
        clean = clean[:max(1, limit - 3)].rstrip() + "..."
    return clean


def sanitize_url(url):
    url = (url or "").replace(" ", "%20")
    url = re.sub(r"[\x00-\x20\x7f]+", "", url)
    return to_cp1254(url, MAX_URL)


def send_line(sock, fields):
    line = "\t".join(str(field) for field in fields) + "\n"
    sock.sendall(line.encode("cp1254", "replace"))


def send_error(sock, query_id, message):
    send_line(sock, ["E", query_id, to_cp1254(message, 60)])


def handle_query(sock, query_id, query):
    query = query.strip()
    if not query:
        send_line(sock, ["R", query_id, "0"])
        send_line(sock, ["END", query_id])
        return

    log(f"sorgu {query_id}: {query!r} ({len(query)} karakter)")
    try:
        results = search_web(query)
    except Exception as exc:
        log(f"sorgu {query_id} basarisiz: {type(exc).__name__}: {exc}")
        send_error(sock, query_id, "Arama yapilamadi")
        return

    send_line(sock, ["R", query_id, str(len(results))])
    for item in results:
        send_line(sock, [
            "T",
            to_cp1254(item["title"], MAX_TITLE),
            to_cp1254(item["site"], MAX_SITE),
            sanitize_url(item["url"]),
            to_cp1254(item["snippet"], MAX_SNIPPET),
        ])
    send_line(sock, ["END", query_id])
    log(f"sorgu {query_id}: {len(results)} sonuc gonderildi")


def connect_qemu(host, port, wait_seconds):
    deadline = time.monotonic() + wait_seconds
    last_error = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), timeout=2.0)
            sock.settimeout(None)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.2)
    raise ConnectionError(f"QEMU COM2 soketine baglanilamadi: {last_error}")


def main():
    parser = argparse.ArgumentParser(
        description="casrev-os COM2 internet arama koprusu"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5568)
    parser.add_argument("--wait", type=float, default=20.0,
                        help="QEMU soketi icin en fazla bekleme (saniye)")
    args = parser.parse_args()

    sock = connect_qemu(args.host, args.port, args.wait)
    log(f"COM2 baglantisi kuruldu: {args.host}:{args.port}")

    reader = sock.makefile("rb")
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass
    try:
        while True:
            raw = reader.readline()
            if not raw:
                break

            line = raw.decode("cp1254", "replace").rstrip("\r\n")
            if not line:
                continue

            parts = line.split("\t")
            if len(parts) < 2:
                continue
            command = parts[0]
            if command == "Q" and len(parts) >= 3:
                handle_query(sock, parts[1], "\t".join(parts[2:]))
            elif command == "P" and len(parts) >= 3:
                handle_visual_page(sock, parts[1], "\t".join(parts[2:]))
            elif command in ("S", "J", "C", "B", "F", "Y") and len(parts) >= 2:
                handle_visual_action(sock, parts[1], command, parts[2:])
    except (ConnectionError, OSError) as exc:
        log(f"baglanti kapandi: {exc}")
    finally:
        try:
            reader.close()
        except OSError:
            pass
        try:
            sock.close()
        except OSError:
            pass
        browser_shutdown()

    log("cikis")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as exc:
        log(f"baslatma hatasi: {type(exc).__name__}: {exc}")
        sys.exit(1)
