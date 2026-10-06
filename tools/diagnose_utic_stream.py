#!/usr/bin/env python3
"""Probe UTIC and HLS without printing credentials, response bodies or URLs."""
import http.cookiejar
import json
import os
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request


def main():
    key = os.getenv('UTIC_API_KEY')
    if not key:
        print('UTIC_API_KEY: missing or empty')
        return
    cctv_id = os.getenv('UTIC_CCTV_ID', 'L010009')
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def fetch(url, referer=None, ajax=False):
        headers = {'User-Agent': 'jetson-traffic-cctv-vision/1.0'}
        if referer:
            headers['Referer'] = referer
        if ajax:
            headers['X-Requested-With'] = 'XMLHttpRequest'
        request = urllib.request.Request(url, headers=headers)
        with opener.open(request, timeout=20) as response:
            return response.read(16 * 1024 * 1024)

    base = 'http://www.utic.go.kr'
    open_url = base + '/guide/cctvOpenData.do?' + urllib.parse.urlencode({'key': key})
    body = fetch(open_url)
    if cctv_id.encode() not in body:
        print('UTIC: requested CCTV missing from response')
        return
    info = json.loads(fetch(base + '/map/getCctvInfoById.do?' +
                           urllib.parse.urlencode({'cctvId': cctv_id}), open_url, True))
    params = {'key': key, 'cctvid': info['CCTVID'], 'cctvName': info['CCTVNAME']}
    params['kind'] = 'Seoul' if info['CCTVID'].startswith('L01') else info.get('KIND', 'undefined')
    for dest, source in [('cctvip', 'CCTVIP'), ('cctvch', 'CH'), ('id', 'ID'),
                         ('cctvpasswd', 'PASSWD'), ('cctvport', 'PORT')]:
        value = info.get(source)
        params[dest] = 'undefined' if value is None else value
    page = fetch(base + '/jsp/map/openDataCctvStream.jsp?' +
                 urllib.parse.urlencode(params), open_url).decode('utf-8', 'replace')
    page = re.sub(
        r'''("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`)|<!--[\s\S]*?-->|/\*[\s\S]*?\*/|//[^\n]*''',
        lambda item: item.group(1) or ' ', page)
    match = re.search(r'https?://[^"\'<>\s]+\.m3u8(?:\?[^"\'<>\s]*)?', page)
    if match:
        url = match.group()
    else:
        match = re.search(r"\bvideo_url\s*=\s*[\"'](https?://[^\"'<>\s]+)[\"']", page)
        if not match:
            print('HLS: URL missing')
            return
        url = match.group(1)
    url = url.replace('&amp;', '&')
    print('HLS URL: found; query parameters:', bool(urllib.parse.urlsplit(url).query), flush=True)
    playlist = fetch(url)
    print('HLS playlist: reachable; EXTM3U:', playlist.lstrip().startswith(b'#EXTM3U'), flush=True)
    try:
        result = subprocess.run(
            ['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_type,width,height',
             '-of', 'json', '-i', url], capture_output=True, timeout=25)
        video = False
        if result.returncode == 0:
            data = json.loads(result.stdout)
            video = any(s.get('codec_type') == 'video' for s in data.get('streams', []))
        print('FFprobe: exit=', result.returncode, 'video=', video)
        if video:
            for stream in data.get('streams', []):
                if stream.get('codec_type') == 'video':
                    width, height = stream.get('width'), stream.get('height')
                    if isinstance(width, int) and isinstance(height, int):
                        print('Video resolution:', width, 'x', height)
    except subprocess.TimeoutExpired:
        print('FFprobe: timed out after 25 seconds')
    except FileNotFoundError:
        print('FFprobe: not installed')


if __name__ == '__main__':
    try:
        main()
    except urllib.error.HTTPError as error:
        print('Probe: HTTP status', error.code)
    except Exception as error:
        # Exceptions may contain credential-bearing URLs; emit type only.
        print('Probe failed:', type(error).__name__)
