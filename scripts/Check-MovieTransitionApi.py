"""Read-only external-agent check against the running opt-in transition GUI demo.

Never prints credentials; creates no projects or edits. Run while the GUI check
is active. --output is an explicitly selected local validation artifact.
"""
import argparse
import json
from pathlib import Path
import time
import urllib.parse
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--heartbeat', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()


def http(url, body=None, token=None):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    req = urllib.request.Request(url, data=None if body is None else json.dumps(body).encode(), headers=headers)
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)


deadline = time.monotonic() + 180
while True:
    try:
        heartbeat = json.loads(args.heartbeat.read_text(encoding='utf-8'))
        discovery = http('http://127.0.0.1:' + str(heartbeat['httpPort']) + '/suite/entry?path=agents/movie.json')
        base, token = discovery['baseUrl'], discovery['token']
        commands = http(base + '/v1/commands', token=token)
        assert all('movie.transition.' + op in commands for op in ('list', 'get', 'catalog', 'validate', 'create', 'update', 'remove'))
        break
    except (OSError, ValueError, KeyError):
        if time.monotonic() >= deadline:
            raise
        time.sleep(.5)


def command(op, body=None, ok=True):
    queued = http(base + '/v1/commands/movie.transition.' + op, body or {}, token)
    end = time.monotonic() + 20
    while time.monotonic() < end:
        request = http(base + '/v1/requests/' + queued['requestId'], token=token)
        if request['status'] in ('completed', 'failed'):
            details = request['details']
            assert details['ok'] is ok, details
            return details.get('data')
        time.sleep(.05)
    raise AssertionError('External transition command timed out')


catalog = command('catalog')
assert len(catalog['types']) == 4
while True:
    transitions = command('list')
    if len(transitions) == 3:
        break
    assert time.monotonic() < deadline, 'Demo did not reach its transition stage'
    time.sleep(.5)
for transition in transitions:
    assert command('get', {'transitionId': transition['id']}) == transition
    assert command('validate', {'transitionId': transition['id']})['valid']
command('validate', {'transitionId': transitions[0]['id'], 'duration': -1}, ok=False)
query = urllib.parse.urlencode({'q': 'movie transition dissolve wipe crossfade handles numerical duration'})
cards = http(base + '/v1/cards/match?' + query, token=token)
matched = [card['id'] for card in cards['cards'] if card['id'].startswith('movie.transitions.')]
assert len(matched) == 2, 'Shipped transition guidance was not retrieved: ' + str(matched)
args.output.write_text(json.dumps({'ok': True, 'commandCount': 7, 'transitionsQueried': len(transitions),
                                  'cardsRetrieved': matched, 'mutations': False}, indent=2), encoding='utf-8')
print('PASS: external transition command schemas/queries/dry-runs and both shipped training cards; no mutations')
