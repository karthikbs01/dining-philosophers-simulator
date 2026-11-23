import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { existsSync } from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';

const root = fileURLToPath(new URL('../', import.meta.url));
const requireFrontend = createRequire(
  new URL('../website/frontend/package.json', import.meta.url),
);
const { chromium } = requireFrontend('playwright-core');
const binaryName =
  process.platform === 'win32' ? 'dining_server.exe' : 'dining_server';
const executable =
  process.env.DINING_SERVER ||
  [
    path.join(root, 'build', binaryName),
    path.join(root, 'build', 'Release', binaryName),
  ].find(existsSync);

async function freePort() {
  const probe = net.createServer();
  probe.listen(0, '127.0.0.1');
  await once(probe, 'listening');
  const { port } = probe.address();
  await new Promise((resolve) => probe.close(resolve));
  return port;
}

test(
  'real server, control API, SSE and React agree through a complete lifecycle',
  { timeout: 75000 },
  async (t) => {
    assert.ok(
      executable,
      'Build dining_server first, or set DINING_SERVER to its full path.',
    );
    assert.ok(
      existsSync(path.join(root, 'website/frontend/dist/index.html')),
      'Run npm run build first.',
    );
    const port = await freePort();
    const url = 'http://127.0.0.1:' + port;
    const server = spawn(executable, [String(port)], {
      cwd: root,
      windowsHide: true,
    });
    const exited = once(server, 'exit');
    t.after(async () => {
      server.kill();
      await exited;
    });
    await Promise.race([
      once(server.stdout, 'data'),
      exited.then(() => {
        throw Error('Server exited early');
      }),
    ]);
    const state = () => fetch(url + '/api/state').then((r) => r.json());
    const post = (action, config) =>
      fetch(url + '/api/' + action, {
        method: 'POST',
        headers: { 'X-Dining-Control': '1' },
        body: new URLSearchParams(config),
      });
    const initial = await state();
    assert.equal(initial.status, 'Idle');
    const request = { ...initial.config, generation: initial.generation };
    assert.equal((await post('start', { ...request, count: 1 })).status, 400);
    assert.equal(
      (await post('start', { ...request, rounds: '2.5' })).status,
      400,
    );
    assert.equal((await post('resume', request)).status, 409);

    const browser = await chromium.launch({
      channel: process.env.BROWSER_CHANNEL || 'chrome',
      executablePath: process.env.CHROME_PATH,
      headless: true,
    });
    t.after(() => browser.close());
    const page = await browser.newPage({
      viewport: { width: 1280, height: 900 },
    });
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    // Simulate a temporarily unavailable transport. All state and events still come from C++.
    await page.route(
      '**/api/events',
      (route) => route.fulfill({ status: 503, body: 'Unavailable' }),
      { times: 1 },
    );
    await page.goto(url, { waitUntil: 'domcontentloaded' });
    await page
      .getByRole('alert')
      .filter({ hasText: 'Connection interrupted' })
      .waitFor();
    const button = (name) => page.getByRole('button', { name, exact: true });
    await page.getByText('Session idle', { exact: true }).waitFor();
    assert.equal(await page.locator('.seat').count(), 5);
    if (process.env.DINING_SCREENSHOT)
      await page.screenshot({ path: process.env.DINING_SCREENSHOT });
    await page.getByLabel('Eating rounds', { exact: true }).fill('2');
    await page.getByLabel('Thinking time', { exact: true }).fill('10');
    await page.getByLabel('Eating time', { exact: true }).fill('240');
    await button('Start simulation').click();
    await page.locator('.fork.held').first().waitFor();
    const running = await state();
    assert.equal(running.config.rounds, 2);
    const active = { ...request, generation: running.generation };
    assert.equal((await post('start', active)).status, 409);
    assert.equal((await post('reset', request)).status, 409);
    await button('Pause').click();
    await page.locator('.session.paused').waitFor();
    const paused = await state();
    await delay(280);
    assert.deepEqual(await state(), paused);
    assert.equal(
      await page.locator('.journal .counter').innerText(),
      paused.seq + ' events',
    );
    assert.deepEqual(
      await page.locator('.fork title').allTextContents(),
      paused.owners.map(
        (owner, i) => 'F' + i + (owner < 0 ? ' is free' : ' held by P' + owner),
      ),
    );
    assert.deepEqual(
      await page.locator('tbody .state-label').allTextContents(),
      paused.people.map((p) => p.state),
    );

    // A new EventSource receives the journal even when an existing session is frozen.
    await page.reload({ waitUntil: 'domcontentloaded' });
    await page.locator('.session.paused').waitFor();
    assert.equal(
      await page.locator('.journal .counter').innerText(),
      paused.seq + ' events',
    );
    await button('Resume').click();
    await page.locator('.session.completed').waitFor();
    const complete = await state();
    assert.equal(complete.status, 'Completed');
    assert.equal(
      await page.locator('.journal .counter').innerText(),
      complete.seq + ' events',
    );
    assert.equal(
      await page.locator('.metric .value').first().innerText(),
      '10 / 10',
    );
    for (const [id, person] of complete.people.entries()) {
      assert.equal(person.meals, 2);
      const cells = page.locator('tbody tr').nth(id).locator('td');
      assert.deepEqual(await cells.allTextContents(), [
        'P' + id,
        'Finished',
        '2',
        person.avgWait.toFixed(1) + ' ms',
        person.maxWait.toFixed(1) + ' ms',
      ]);
    }
    const older = button('Show older events');
    if (await older.isVisible()) await older.click();
    assert.deepEqual(
      await page.locator('.log-row span').allTextContents(),
      complete.events.map((e) => e.message).reverse(),
    );

    await button('Start simulation').click();
    await page.locator('.session.running').waitFor();
    await button('Pause').click();
    await page.locator('.session.paused').waitFor();
    await button('Reset').click();
    await page.getByText('Session idle', { exact: true }).waitFor();
    const reset = await state();
    assert.equal(reset.seq, 0);
    assert.ok(reset.owners.every((owner) => owner === -1));
    assert.equal(
      await page.locator('.journal .counter').innerText(),
      '0 events',
    );
    const newRequest = {
      ...request,
      generation: reset.generation,
      thinkMs: 60000,
    };
    const starts = await Promise.all([
      post('start', newRequest),
      post('start', newRequest),
    ]);
    assert.deepEqual(starts.map((r) => r.status).sort(), [204, 409]);
    const latest = await state();
    assert.equal(
      (await post('reset', { generation: latest.generation })).status,
      204,
    );
    await page.setViewportSize({ width: 360, height: 770 });
    assert.equal(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
      true,
    );
    assert.deepEqual(errors, []);
  },
);
