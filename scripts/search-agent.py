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
import html
import json
import re
import socket
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from html.parser import HTMLParser

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
        while True:
            raw = reader.readline()
            if not raw:
                break

            line = raw.decode("cp1254", "replace").rstrip("\r\n")
            if not line:
                continue

            fields = line.split("\t", 2)
            if len(fields) < 3 or fields[0] != "Q":
                continue

            handle_query(sock, fields[1], fields[2])
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
