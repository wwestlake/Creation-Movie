"""Focused live-app check. Uses only disposable projects created by this run.

Run after building, with Movie closed. Does not stop/restart the shared VFS service.
Requires explicit --heartbeat and --media paths. Test outputs go to build/.
"""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--heartbeat', type=Path, required=True)
parser.add_argument('--media', type=Path, required=True)
parser.add_argument('--tracks', action='store_true', help='Also verify track commands and version-2 VFS migration')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
tag = 'Movie lifecycle check ' + str(uuid.uuid4())
vfs = 'http://127.0.0.1:' + str(json.loads(args.heartbeat.read_text())['httpPort'])
token = None
base = None
created = {}
checks = []

def http(url, body=None, method=None, auth=False):
    headers = {'Content-Type': 'application/json'}
    if auth:
        headers['Authorization'] = 'Bearer ' + token
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=12) as response:
        payload = response.read()
        return json.loads(payload) if payload else None

def get(path):
    return http(base + path, auth=True)

def command(name, body=None, expected_error=None):
    queued = http(base + '/v1/commands/' + name, body or {}, auth=True)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        response = get('/v1/requests/' + queued['requestId'])
        if response['status'] in ('completed', 'failed'):
            details = response['details']
            if expected_error:
                assert not details['ok'] and details['error']['code'] == expected_error, response
            else:
                assert details['ok'], response
            return details.get('data')
        time.sleep(.05)
    raise AssertionError('Command timeout: ' + name)

def wait_job(result, expected='completed'):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        job = get('/v1/app/jobs')
        assert job['id'] == result['jobId'], ('Job was replaced before completion', job)
        if job['status'] in ('completed', 'failed', 'cancelled'):
            assert job['status'] in expected.split('|'), job
            return job
        time.sleep(.1)
    raise AssertionError('Background job timeout')

def delete_created(project_id):
    # The exact id must have been returned by this run's create command, and the
    # service must still identify it by our unique name. Never delete by search.
    assert project_id in created
    query = urllib.parse.urlencode({'projectId': project_id})
    manifest = http(vfs + '/project/manifest?' + query)
    assert manifest['projectName'] == created[project_id], manifest
    http(vfs + '/project?' + query, method='DELETE')
    del created[project_id]

def passed(label):
    checks.append(label)
    print('PASS:', label, flush=True)

# Refuse to take over an app that somebody already has open.
try:
    existing = http(vfs + '/suite/entry?path=agents/movie.json')
    token, base = existing['token'], existing['baseUrl']
    try:
        get('/v1/status')
    except (OSError, urllib.error.URLError):
        pass
    else:
        raise RuntimeError('Movie is already running. Leave that session alone; close it before this check.')
except urllib.error.HTTPError as exc:
    if exc.code != 404:
        raise
token = base = None
temp = root / 'build' / 'temp'
temp.mkdir(exist_ok=True)
env = dict(os.environ, TEMP=str(temp), TMP=str(temp))
process = subprocess.Popen([str(root / 'build/CreationMovieApp_artefacts/Debug/Djehuti Movie.exe')], env=env)
try:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        try:
            discovery = http(vfs + '/suite/entry?path=agents/movie.json')
            token, base = discovery['token'], discovery['baseUrl']
            get('/v1/status')
            break
        except (OSError, ValueError, KeyError):
            time.sleep(.2)
    assert base and process.poll() is None, 'Movie did not start'
    original = get('/v1/app/state')['projectId']
    a = wait_job(command('movie.project.new', {'name': tag + ' A', 'width': 640, 'height': 360}))['result']['projectId']
    created[a] = tag + ' A'
    state = get('/v1/app/state')
    assert state['projectId'] == a and not state['dirty']
    passed('New project is applied and saved before its job reports completed')

    importing = command('movie.media.import', {'path': str(args.media.resolve())})
    command('movie.project.new', {'name': 'must not be created'}, expected_error='job_busy')
    command('movie.app.close', expected_error='job_busy')
    imported = wait_job(importing)['result']['id']
    state = get('/v1/app/state')
    assert state['projectId'] == a and any(m['id'] == imported for m in state['sequence']['media'])
    passed('Import blocks project switching/closing and completion includes applied media')

    command('movie.project.new', {'name': 'must not discard edits'}, expected_error='unsaved_changes')
    command('movie.timeline.place', {'assetId': imported, 'duration': 1.0})
    saved = wait_job(command('movie.project.save'))
    assert saved['result']['projectId'] == a and not get('/v1/app/state')['dirty']
    passed('Unsaved replacement is rejected; asynchronous save clears dirty after success')

    failed_open = command('movie.project.open', {'projectId': str(uuid.uuid4())})
    wait_job(failed_open, 'failed')
    assert get('/v1/app/state')['projectId'] == a
    passed('Failed open preserves the active document and project identity')

    wait_job(command('movie.project.open', {'projectId': a}))
    assert len(get('/v1/app/state')['sequence']['clips']) == 1
    if args.tracks:
        ffmpeg = shutil.which('ffmpeg')
        assert ffmpeg, 'Track compatibility check requires an existing ffmpeg executable on PATH'
        sequence = get('/v1/app/sequence')
        owner = sequence['clips'][0]['trackId']
        command('movie.track.rename', {'trackId': owner, 'name': 'Track foundation check'})
        command('movie.track.source', {'trackId': owner, 'source': {'category': 'camera', 'identity': 'test-config-only'}})
        command('movie.track.preferences', {'trackId': owner, 'armed': True, 'monitoring': True})
        command('movie.track.reorder', {'trackId': owner, 'order': 3})
        assert get('/v1/app/sequence')['clips'][0]['trackId'] == owner
        command('movie.timeline.undo')
        assert get('/v1/app/sequence')['tracks'][0]['id'] == owner
        command('movie.timeline.redo')
        command('movie.track.remove', {'trackId': owner}, expected_error='edit_rejected')
        wait_job(command('movie.project.save'))
        wait_job(command('movie.project.open', {'projectId': a}))
        track = command('movie.track.get', {'trackId': owner})
        assert track['name'] == 'Track foundation check' and track['armed'] and track['monitoring']
        assert track['source']['identity'] == 'test-config-only'
        assert command('movie.track.list')[-1]['id'] == owner
        passed('Track commands, stable ownership, undo/redo and source preferences survive VFS save/reopen')

        before_export = root / 'build' / ('track-before-' + str(uuid.uuid4()) + '.mkv')
        wait_job(command('movie.export', {'path': str(before_export), 'preset': 'ffv1'}))

        # Only rewrite the exact disposable project created by this run.
        assert a in created
        legacy = get('/v1/app/sequence')
        order = {t['id']: i for i, t in enumerate(legacy.pop('tracks'))}
        for clip in legacy['clips']:
            clip['track'] = order[clip.pop('trackId')]
        legacy['version'] = 2
        url = vfs + '/project/entry?' + urllib.parse.urlencode({'projectId': a, 'path': 'Project/movie-state.json'})
        http(url, legacy, method='PUT')
        wait_job(command('movie.project.open', {'projectId': a}))
        migrated = get('/v1/app/sequence')
        assert migrated['version'] == 3 and len(migrated['tracks']) == 8
        for old, new in zip(legacy['clips'], migrated['clips']):
            assert new['trackId'] == 'legacy-track-' + str(old['track'])
            for key in ('assetId', 'startFrame', 'durationFrames', 'sourceIn', 'params'):
                assert old[key] == new[key]
        assert http(url)['version'] == 2, 'Opening migration wrote the original'
        wait_job(command('movie.project.save'))
        wait_job(command('movie.project.open', {'projectId': a}))
        assert get('/v1/app/sequence') == migrated
        command('movie.transport.play')
        time.sleep(.4)
        transport = get('/v1/app/state')['transport']
        assert transport['position'] > 0 and not transport['error'], transport
        command('movie.transport.stop')
        passed('Version-2 VFS migration preserves placements, writes only on save, reopens and plays')
        after_export = root / 'build' / ('track-after-' + str(uuid.uuid4()) + '.mkv')
        wait_job(command('movie.export', {'path': str(after_export), 'preset': 'ffv1'}))
        def decoded_hash(path, mapping):
            return subprocess.check_output([ffmpeg, '-v', 'error', '-i', str(path), '-map', mapping,
                                            '-f', 'hash', '-hash', 'sha256', '-'], timeout=30)
        for mapping in ('0:v:0', '0:a:0'):
            assert decoded_hash(before_export, mapping) == decoded_hash(after_export, mapping)
        passed('Decoded lossless video and audio are identical before and after version-2 migration')
    exported_path = root / 'build' / ('lifecycle-export-' + str(uuid.uuid4()) + '.mp4')
    wait_job(command('movie.export', {'path': str(exported_path), 'preset': 'mp4'}))
    assert exported_path.stat().st_size > 1000
    passed('Saved edits reopen and a short real recording exports')

    command('movie.timeline.title', {'text': 'Failed-save check', 'duration': 1.0})
    delete_created(a)  # Inject write failure in OUR disposable project only.
    closing = command('movie.app.close')
    assert closing['savePending']
    pending = get('/v1/app/jobs')
    wait_job({'jobId': pending['id']}, 'failed')
    state = get('/v1/app/state')
    assert process.poll() is None and state['dirty'] and state['job']['error']
    passed('Failed save-on-close keeps the app alive with dirty edits and an error')

    # Restore the saved project that startup opened, without modifying it.
    wait_job(command('movie.project.open', {'projectId': original, 'discardChanges': True}))
    http(base + '/v1/commands/movie.app.close', {}, auth=True)
    process.wait(timeout=15)
    passed('Clean close exits normally after recovery from a failed save')
    result_name = 'track-lifecycle-results.json' if args.tracks else 'lifecycle-results.json'
    (root / 'build' / result_name).write_text(json.dumps({'checks': checks, 'export': str(exported_path)}, indent=2))
except BaseException:
    if args.tracks and process.poll() is None:
        try:
            command('movie.job.cancel')
            pending = get('/v1/app/jobs')
            if pending['status'] == 'running':
                wait_job({'jobId': pending['id']}, 'completed|failed|cancelled')
            if 'original' in globals():
                wait_job(command('movie.project.open', {'projectId': original, 'discardChanges': True}))
            http(base + '/v1/commands/movie.app.close', {}, auth=True)
            process.wait(timeout=15)
        except Exception as cleanup_error:
            print('Could not close this test instance:', cleanup_error, flush=True)
    print('Check failed. The app is left available for inspection; process id:', process.pid, flush=True)
    raise
