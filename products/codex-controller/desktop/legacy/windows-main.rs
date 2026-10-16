mod audio;
mod bridge;
mod config;
mod connection;
mod logic;
mod pairing;
mod wire;
use bridge::{Command, Shared};
use config::{Config, Session};
use std::sync::{Arc, Mutex};
use tokio::sync::mpsc;
fn font_definitions() -> eframe::egui::FontDefinitions {
    let mut fonts = eframe::egui::FontDefinitions::default();
    fonts.font_data.insert(
        "passport-cjk".into(),
        Arc::new(eframe::egui::FontData::from_static(include_bytes!(
            "../../../../assets/fonts/NotoSansSC-Regular.otf"
        ))),
    );
    fonts
        .families
        .get_mut(&eframe::egui::FontFamily::Proportional)
        .unwrap()
        .push("passport-cjk".into());
    fonts
}

struct App {
    config: Config,
    shared: Shared,
    commands: mpsc::UnboundedSender<Command>,
    tab: usize,
    name: String,
    url: String,
    error: String,
    outputs: Vec<String>,
    pin_input: String,
}
impl App {
    fn new(
        cc: &eframe::CreationContext<'_>,
        config: Config,
        shared: Shared,
        commands: mpsc::UnboundedSender<Command>,
    ) -> Self {
        cc.egui_ctx.set_fonts(font_definitions());
        cc.egui_ctx.set_theme(eframe::egui::Theme::Dark);
        cc.egui_ctx.set_visuals(eframe::egui::Visuals::dark());
        cc.egui_ctx.style_mut(|style| {
            style.spacing.item_spacing = eframe::egui::vec2(12.0, 12.0);
            style.spacing.button_padding = eframe::egui::vec2(14.0, 9.0);
            style.spacing.interact_size.y = 36.0;
            style.text_styles.insert(
                eframe::egui::TextStyle::Body,
                eframe::egui::FontId::proportional(16.0),
            );
            style.text_styles.insert(
                eframe::egui::TextStyle::Button,
                eframe::egui::FontId::proportional(16.0),
            );
            style.text_styles.insert(
                eframe::egui::TextStyle::Heading,
                eframe::egui::FontId::proportional(25.0),
            );
        });
        let (outputs, error) = match audio::outputs() {
            Ok(outputs) => (outputs, String::new()),
            Err(e) => (vec![], format!("无法列出音频输出：{e:#}")),
        };
        Self {
            config,
            shared,
            commands,
            tab: 0,
            name: String::new(),
            url: String::new(),
            error,
            outputs,
            pin_input: String::new(),
        }
    }
    fn apply(&mut self) {
        match self.config.validate().and_then(|_| self.config.save()) {
            Ok(()) => {
                let _ = self.commands.send(Command::Apply(self.config.clone()));
                self.error.clear();
            }
            Err(e) => self.error = format!("{e:#}"),
        }
    }
}
impl App {
    fn render(&mut self, ctx: &eframe::egui::Context) {
        use eframe::egui;
        ctx.request_repaint_after(std::time::Duration::from_millis(150));
        let pin = self.shared.lock().unwrap().pin.clone();
        if pin.is_pending() {
            egui::Window::new("输入设备配对码")
                .collapsible(false)
                .resizable(false)
                .show(ctx, |ui| {
                    ui.label("输入 Passport 当前显示的六位数字。配对码不会保存。");
                    ui.add(
                        egui::TextEdit::singleline(&mut self.pin_input)
                            .password(true)
                            .char_limit(6),
                    );
                    ui.horizontal(|ui| {
                        if ui.button("确认配对").clicked() {
                            match pin.submit(&self.pin_input) {
                                Ok(()) => {
                                    self.pin_input.clear();
                                    self.error.clear();
                                }
                                Err(e) => self.error = e.to_string(),
                            }
                        }
                        if ui.button("取消配对").clicked() {
                            pin.cancel();
                            self.pin_input.clear();
                        }
                    });
                });
        } else {
            self.pin_input.clear();
        }
        egui::TopBottomPanel::top("header").show(ctx, |ui| {
            ui.add_space(10.0);
            ui.heading("Passport Codex Controller");
            ui.label("蓝牙会话控制器与虚拟麦克风");
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                for (i, title) in ["设备连接", "会话清单", "音频路由"].iter().enumerate()
                {
                    ui.selectable_value(&mut self.tab, i, *title);
                }
            });
            ui.add_space(8.0);
        });
        let (text, error, devices, mic, connected, selected, level, phase, stopped) = {
            let s = self.shared.lock().unwrap();
            (
                s.text.clone(),
                s.error.clone(),
                s.devices.clone(),
                s.mic,
                s.connected,
                s.selected,
                s.level,
                s.connection.phase,
                s.stopped,
            )
        };
        egui::TopBottomPanel::bottom("status").show(ctx, |ui| {
            ui.add_space(6.0);
            ui.horizontal(|ui| {
                if phase.busy() {
                    ui.spinner();
                }
                ui.strong(phase.label());
                ui.label(text);
            });
            ui.label(if mic {
                "麦克风已开启"
            } else {
                "麦克风已关闭"
            });
            if !error.is_empty() {
                ui.colored_label(egui::Color32::LIGHT_RED, error);
            }
            if !self.error.is_empty() {
                ui.colored_label(egui::Color32::LIGHT_RED, &self.error);
            }
        });
        egui::CentralPanel::default().frame(egui::Frame::central_panel(&ctx.style()).inner_margin(24.0)).show(ctx,|ui|{egui::ScrollArea::vertical().show(ui,|ui|{
            ui.add_space(20.0);
            ui.set_max_width(780.0);
            match self.tab {
                0=>{
                    ui.heading("连接 AI Passport");
                    ui.label("首次连接时，Windows 在本程序输入设备显示的六位配对码；macOS 按系统提示配对。");
                    ui.label("Windows 与 macOS 一次只能连接一台电脑；请先断开另一台桥接。");ui.add_space(12.0);
                    ui.horizontal(|ui|{
                        if ui.add_enabled(!stopped && !connected && !phase.busy(),egui::Button::new("重新扫描")).clicked(){
                            let mut s=self.shared.lock().unwrap();
                            if s.connection.scan(){s.error.clear();let _=self.commands.send(Command::Scan);}
                        }
                        if ui.add_enabled(connected || matches!(phase,connection::Phase::Connecting | connection::Phase::Syncing), egui::Button::new(if connected {"断开连接"} else {"取消连接"})).clicked(){
                            self.shared.lock().unwrap().pin.cancel();
                            let _=self.commands.send(Command::Disconnect);
                        }
                    });
                    if devices.is_empty() && !phase.busy() && !stopped {
                        ui.add_space(24.0);
                        ui.strong("还没有发现 Passport");
                        ui.label("打开设备并放在 Mac 附近，然后点击重新扫描。首次使用请允许本程序访问蓝牙。");
                    }
                    for d in devices {ui.horizontal(|ui|{ui.label(&d.name);
                        if ui.add_enabled(!stopped && !connected && !phase.busy(),egui::Button::new("连接")).clicked(){
                            let mut s=self.shared.lock().unwrap();
                            if let Some(epoch)=s.connection.connect(&d.id){
                                s.error.clear();self.config.peripheral=d.id.clone();
                                let _=self.commands.send(Command::Connect(d.id,epoch));
                            }
                        }
                    });}
                    if phase == connection::Phase::Failed {
                        ui.add_space(12.0);
                        ui.label("请先查看下方具体错误。检查蓝牙权限与设备距离，再重新扫描或连接。仅在确认绑定不一致时重置配对。");
                    }
                    if connected { ui.label("已就绪。到会话清单配置目标，再到音频路由选择虚拟音频输出。"); }
                }
                1=>{
                    ui.heading("设备上的会话清单");
                    ui.label("在 Codex 复制会话深链接，粘贴到这里。上下键按此清单循环打开会话。");
                    ui.label("名称显示所选目标，不跟踪你在 Codex 手动切换的会话。");ui.add_space(10.0);
                    if self.config.sessions.is_empty() {
                        ui.strong("添加第一个会话");
                        ui.label("填写名称和会话链接，添加后保存并同步。最多支持 16 个会话。");
                    }
                    let mut remove=None;let mut swap=None;
                    for (i,s) in self.config.sessions.iter_mut().enumerate(){
                        ui.group(|ui|{ui.horizontal(|ui|{
                            ui.label(if connected && i==selected {"已选择"} else {"会话"});ui.text_edit_singleline(&mut s.name);
                            if ui.add_enabled(i>0,egui::Button::new("上移")).clicked(){swap=Some((i,i-1));}
                            if ui.button("删除").clicked(){remove=Some(i);}
                        });ui.text_edit_singleline(&mut s.url);});
                    }
                    if let Some(i)=remove{self.config.sessions.remove(i);self.config.selected=0;}
                    if let Some((a,b))=swap{self.config.sessions.swap(a,b);self.config.selected=0;}
                    ui.add_space(12.0);ui.label("显示名称（常用简体中文或英文）");ui.text_edit_singleline(&mut self.name);
                    ui.label("会话链接：codex://threads/<id>");ui.add(egui::TextEdit::singleline(&mut self.url).desired_width(f32::INFINITY).hint_text("codex://threads/…"));
                    if ui.add_enabled(self.config.sessions.len()<16,egui::Button::new("添加会话")).clicked(){
                        let result=config::validate_name(&self.name).and_then(|_|config::validate_url(&self.url));
                        match result {Ok(())=>{self.config.sessions.push(Session{name:std::mem::take(&mut self.name),url:std::mem::take(&mut self.url)});self.error.clear();},Err(e)=>self.error=e.to_string()}
                    }
                    if ui.button("保存并同步到设备").clicked(){if self.config.sessions.is_empty(){self.config.selected=0;}self.apply();}
                }
                _=>{
                    ui.heading("设置虚拟麦克风");
                    ui.label(if cfg!(target_os="macos") {"安装 BlackHole 2ch；本程序输出到 BlackHole，Codex 选择 BlackHole 作为麦克风。"}
                        else {"安装 VB-CABLE；本程序输出到 CABLE Input，Codex 选择 CABLE Output 作为麦克风。"});
                    ui.hyperlink_to("VB-CABLE 官方下载","https://vb-audio.com/Cable/");
                    ui.hyperlink_to("BlackHole 开源项目","https://github.com/ExistentialAudio/BlackHole");
                    ui.add_space(16.0);
                    ui.strong(if cfg!(target_os="macos") {"Passport → 本程序解码 → BlackHole → Codex 语音输入"} else {"Passport → 本程序解码 → VB-CABLE → Codex 语音输入"});
                    if self.outputs.is_empty() { ui.label("未发现音频输出。安装 BlackHole 后点击刷新设备。"); }
                    let route_available = self.outputs.iter().any(|name| name == &self.config.output);
                    if !route_available { ui.colored_label(egui::Color32::LIGHT_YELLOW, "当前输出不可用。请安装虚拟音频驱动或选择可用端点，然后保存。"); }
                    ui.label("音频输出设备（不是麦克风输入端）");
                    egui::ComboBox::from_id_salt("audio_device").selected_text(&self.config.output).show_ui(ui,|ui|{
                        for name in &self.outputs{ui.selectable_value(&mut self.config.output,name.clone(),name);}
                    });
                    ui.horizontal(|ui|{
                        if ui.button("刷新设备").clicked(){match audio::outputs(){Ok(outputs)=>{self.outputs=outputs;self.error.clear();},Err(e)=>self.error=format!("无法列出音频输出：{e:#}")}}
                        if ui.button("保存音频路由").clicked(){self.apply();}
                        if ui.add_enabled(!mic && route_available,egui::Button::new("播放 1 kHz 测试音")).clicked(){let _=self.commands.send(Command::Tone);}
                    });
                    ui.add_space(12.0);ui.add(egui::ProgressBar::new(level as f32/100.0).text(format!("桌面解码电平  {level}%")));
                    {
                        let s=self.shared.lock().unwrap();
                        ui.label(format!("本次音频：收到 {} 包 · 解码 {} 包 · 队列丢弃 {} 包 · 解码错误 {} 包", s.audio_packets, s.decoded_packets, s.dropped_packets, s.decode_errors));
                    }
                    ui.label("解码电平只证明音频到达本程序。还需用测试音录回验证 BlackHole，并在 Codex 中选择对应输入。");
                    ui.label("确认键仅控制音频传送。请手动打开 Codex 的语音输入界面；关闭本程序会停止音频。");
                    ui.label("虚拟驱动需独立安装；本程序不会更改系统默认音频设备，也不会保存录音。");
                }
            }
        });});
    }
}
impl eframe::App for App {
    fn update(&mut self, ctx: &eframe::egui::Context, _frame: &mut eframe::Frame) {
        self.render(ctx);
    }
    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        self.shared.lock().unwrap().pin.cancel();
        self.shared.lock().unwrap().quit = true;
        let _ = self.commands.send(Command::Quit);
    }
}
fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.first().map(String::as_str) == Some("ble-check") {
        return tokio::runtime::Runtime::new()?
            .block_on(bridge::check_connection(args.get(1).map(String::as_str)));
    }
    if args.first().map(String::as_str) == Some("devices") {
        for name in audio::outputs()? {
            println!("{name}");
        }
        return Ok(());
    }
    if args.first().map(String::as_str) == Some("inputs") {
        for name in audio::inputs()? {
            println!("{name}");
        }
        return Ok(());
    }
    if args.first().map(String::as_str) == Some("loopback") {
        let output = args
            .get(1)
            .ok_or_else(|| anyhow::anyhow!("loopback requires output and input names"))?;
        let input = args
            .get(2)
            .ok_or_else(|| anyhow::anyhow!("loopback requires output and input names"))?;
        println!("{}", audio::loopback(output, input)?);
        return Ok(());
    }
    let mut config = Config::load()?;
    if args.first().map(String::as_str) == Some("tone") {
        let client = audio::AudioClient::new();
        let output = args.get(1).unwrap_or(&config.output);
        client.tone(output)?;
        std::thread::sleep(std::time::Duration::from_millis(400));
        return Ok(());
    }
    let headless = args.iter().any(|a| a == "--headless");
    for pair in args.windows(2) {
        match pair[0].as_str() {
            "--device" => config.peripheral = pair[1].clone(),
            "--output" => config.output = pair[1].clone(),
            _ => {}
        }
    }
    let shared = Arc::new(Mutex::new(bridge::Status::default()));
    let (tx, rx) = mpsc::unbounded_channel();
    let worker_shared = shared.clone();
    let worker_config = config.clone();
    let worker = std::thread::spawn(move || {
        tokio::runtime::Runtime::new()
            .unwrap()
            .block_on(bridge::run(worker_config, worker_shared, rx));
    });
    if headless {
        println!("Passport bridge running. Press Enter to stop.");
        std::thread::sleep(std::time::Duration::from_secs(4));
        {
            let s = shared.lock().unwrap();
            for d in &s.devices {
                println!("{} {}", d.id, d.name);
            }
            if !s.error.is_empty() {
                eprintln!("{}", s.error);
            }
        }
        let mut line = String::new();
        std::io::stdin().read_line(&mut line)?;
        shared.lock().unwrap().quit = true;
        let _ = tx.send(Command::Quit);
    } else {
        let options = eframe::NativeOptions {
            viewport: eframe::egui::ViewportBuilder::default()
                .with_inner_size([880.0, 680.0])
                .with_min_inner_size([640.0, 480.0]),
            ..Default::default()
        };
        let quit = tx.clone();
        let quit_shared = shared.clone();
        let result = eframe::run_native(
            "Passport Codex Controller",
            options,
            Box::new(move |cc| Ok(Box::new(App::new(cc, config, shared, tx)))),
        );
        let _ = quit.send(Command::Quit);
        quit_shared.lock().unwrap().quit = true;
        if let Err(e) = result {
            let _ = worker.join();
            return Err(anyhow::anyhow!(e.to_string()));
        }
    }
    let _ = worker.join();
    Ok(())
}

#[cfg(test)]
mod ui_tests {
    #[test]
    fn all_pages_with_sixteen_sessions_at_minimum_size() {
        let ctx = eframe::egui::Context::default();
        ctx.set_fonts(super::font_definitions());
        let (tx, _rx) = tokio::sync::mpsc::unbounded_channel();
        let config = crate::config::Config {
            sessions: (0..16)
                .map(|i| crate::config::Session {
                    name: format!("中文会话 {i} {}", "长名称".repeat(8)),
                    url: format!("codex://threads/example-{i}"),
                })
                .collect(),
            ..Default::default()
        };
        let shared = std::sync::Arc::new(std::sync::Mutex::new(crate::bridge::Status::default()));
        let mut app = super::App {
            config,
            shared,
            commands: tx,
            tab: 0,
            name: String::new(),
            url: String::new(),
            error: String::new(),
            outputs: vec![],
            pin_input: String::new(),
        };
        // Render the actual egui page logic without opening Bluetooth, audio or OS links.
        for tab in 0..3 {
            app.tab = tab;
            let input = eframe::egui::RawInput {
                screen_rect: Some(eframe::egui::Rect::from_min_size(
                    eframe::egui::Pos2::ZERO,
                    eframe::egui::vec2(560.0, 420.0),
                )),
                ..Default::default()
            };
            let out = ctx.run(input, |ctx| app.render(ctx));
            assert!(!out.shapes.is_empty());
        }
    }
    #[test]
    fn embedded_chinese_font_renders_inventory() {
        let ctx = eframe::egui::Context::default();
        ctx.set_fonts(super::font_definitions());
        let points: Vec<u32> =
            serde_json::from_str(include_str!("../../font-codepoints.json")).unwrap();
        let _ = ctx.run(Default::default(), |ctx| {
            ctx.fonts(|fonts| {
                let id = eframe::egui::FontId::proportional(16.0);
                for p in &points {
                    assert!(
                        fonts.has_glyph(&id, char::from_u32(*p).unwrap()),
                        "Missing glyph U+{p:04X}"
                    );
                }
            });
        });
    }
}
