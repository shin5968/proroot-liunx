const {chromium}=require('playwright');
const fs=require('fs'),path=require('path'),assert=require('assert');
(async()=>{
process.chdir(path.resolve(__dirname,'../..'));
const browser=await chromium.launch({headless:true,...(process.env.TERMINAL_TEST_BROWSER?{executablePath:process.env.TERMINAL_TEST_BROWSER}:{})});
const page=await browser.newPage({viewport:{width:390,height:740},hasTouch:true,isMobile:true});
let errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.route('https://app.local/**',route=>{const name=new URL(route.request().url()).pathname.slice(1)||'index.html';route.fulfill({contentType:name.endsWith('.html')?'text/html; charset=utf-8':name.endsWith('.js')?'application/javascript':'text/css',path:path.resolve('app/src/main/assets/terminal',name)});});
await page.addInitScript(()=>{window.inputs=[];window.copied='';window.Host={input:s=>inputs.push(s),modifiersChanged(){},resize(){},viewportReady(){},ready(){},copySelection:s=>copied=s,pasteClipboard:()=>pasteText('PASTED_TEXT')};window.Keys={key:s=>inputs.push(s)};});
await page.goto('https://app.local/index.html');await page.waitForTimeout(200);
await page.evaluate(()=>new Promise(r=>terminal.write('hello SELECT_ME world\r\nsecond line',r)));
assert.equal(await page.evaluate(()=>terminal.options.theme.background),'#000000');
await page.evaluate(()=>{extraKey('CTRL');send('c');extraKey('CTRL');extraKey('ALT');send('x');extraKey('ALT');extraKey('↑');});
assert.deepEqual(await page.evaluate(()=>inputs.splice(0)),['\x03','\x1bx','\x1b[A']);
const point=await page.evaluate(()=>{const {r,w,h}=geometry();return {x:r.left+w*9,y:r.top+h/2};});
const cdp=await page.context().newCDPSession(page);
await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[point]});await page.waitForTimeout(650);
await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
assert.equal(await page.evaluate(()=>terminal.getSelection()),'SELECT_ME');
await page.touchscreen.tap(330,600);
assert.equal(await page.evaluate(()=>terminal.hasSelection()),false,'tap outside must dismiss selection');
await page.evaluate(()=>new Promise(r=>terminal.write('\r\n'+Array.from({length:150},(_,i)=>'SCROLL_'+i).join('\r\n'),r)));
await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:200,y:200}]});
for(let y=220;y<=520;y+=30)await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:200,y}]});
await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
await page.waitForTimeout(650);
assert.equal(await page.evaluate(()=>terminal.hasSelection()),false,'swipe must never select');
assert((await page.evaluate(()=>terminal.buffer.active.viewportY)) < (await page.evaluate(()=>terminal.buffer.active.baseY)),'swipe must scroll history');
await page.evaluate(()=>{area.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:200,clientY:300}));});
assert.equal(await page.evaluate(()=>terminal.hasSelection()),false,'touch-generated context menu must not select');
await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:200,y:200}]});
await cdp.send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});
await page.waitForTimeout(550);
assert.equal(await page.evaluate(()=>terminal.hasSelection()),false,'cancel must clear hold timer');
await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:200,y:200}]});
await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:200,y:240}]});
await page.waitForTimeout(550);
assert.equal(await page.evaluate(()=>terminal.hasSelection()),false,'pause during swipe must not select');
await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});

await page.evaluate(()=>{terminal.scrollToTop();longPress(85,12);});
const handle=await page.locator('#end').boundingBox();
await page.mouse.move(handle.x+12,handle.y+12);await page.mouse.down();await page.mouse.move(handle.x+35,handle.y+12);await page.mouse.up();
assert((await page.evaluate(()=>terminal.getSelection())).startsWith('SELECT_ME'));
await page.evaluate(()=>{anchor=6;extent=15;renderSelection();});
await page.locator('#copy').click();assert.equal(await page.evaluate(()=>copied),'SELECT_ME');
await page.evaluate(()=>longPress(20,150));await page.locator('#paste').click();
assert.deepEqual(await page.evaluate(()=>inputs.splice(0)),['PASTED_TEXT']);
await page.evaluate(()=>{extraKey('CTRL');extraKey('ALT');pasteText('literal clipboard');extraKey('CTRL');extraKey('ALT');});
assert.deepEqual(await page.evaluate(()=>inputs.splice(0)),['literal clipboard']);

await page.evaluate(()=>new Promise(r=>terminal.write('\x1b[?2004h',r)));
await page.evaluate(()=>pasteText('a\nb'));
assert.deepEqual(await page.evaluate(()=>inputs.splice(0)),['\x1b[200~a\rb\x1b[201~']);

await page.evaluate(()=>{longPress(20,15);anchor=0;extent=terminal.cols+6;renderSelection();});
assert((await page.evaluate(()=>terminal.getSelection())).includes('second'));

// Android bridge branch: toolbar is native, handles remain in the terminal.
await page.evaluate(()=>{
 hideSelection();window.nativeMenus=[];window.keyboardRequests=0;window.hiddenMenus=0;
 Host.selectionMenu=(...args)=>nativeMenus.push(args);
 Host.hideSelectionMenu=()=>hiddenMenus++;
 Host.keyboard=()=>keyboardRequests++;
 terminal.scrollToTop();longPress(85,12);
});
assert.equal(await page.locator('#tools').isVisible(),false,'native toolbar must replace HTML toolbar');
assert((await page.evaluate(()=>nativeMenus.length))>0);
assert.equal(await page.evaluate(()=>nativeMenus.at(-1)[4]),true);
const nativeHandle=await page.locator('#end').boundingBox();
await page.mouse.move(nativeHandle.x+19,nativeHandle.y+10);await page.mouse.down();
assert.equal(await page.evaluate(()=>dragging),true);
assert((await page.evaluate(()=>hiddenMenus))>0,'hide native menu while dragging');
await page.mouse.move(nativeHandle.x+30,nativeHandle.y+10);await page.mouse.up();
assert.equal(await page.evaluate(()=>dragging),false);
await page.evaluate(()=>selectionAction('copy'));
assert.equal(await page.evaluate(()=>selecting),false);
await page.touchscreen.tap(300,550);
assert((await page.evaluate(()=>keyboardRequests))>0,'tap opens IME through native bridge');
await page.evaluate(()=>{terminal.scrollToBottom();stopFling();});
await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:200,y:180}]});
for(let y=210;y<=450;y+=30){
 await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:200,y}]});
 await page.waitForTimeout(15);
}
await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
const released=await page.evaluate(()=>terminal.buffer.active.viewportY);
await page.waitForTimeout(120);
assert((await page.evaluate(()=>terminal.buffer.active.viewportY))<released,'fling continues after finger release');
await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:200,y:400}]});
assert.equal(await page.evaluate(()=>flingFrame),0,'touch stops momentum');
await cdp.send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});
assert.equal(await page.evaluate(()=>terminal.hasSelection()),false);
assert.deepEqual(errors,[]);console.log('PASS: scroll, tap dismissal, hold cancellation, selection handles, copy/paste, bracketed paste, native menu, keyboard bridge and momentum scrolling');
await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});



