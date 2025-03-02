<div align="center">
  <h1>🎮 EnetPlayGround</h1>
  <p><strong>My personal networking sandbox where packets go to play!</strong></p>
  
  <p>
    <a href="#-cool-stuff-im-playing-with">Cool Stuff</a> •
    <a href="#-screenshots">Screenshots</a> •
    <a href="#-the-grand-blueprint">The Blueprint</a> •
    <a href="#-how-to-join-the-fun">Get It Running</a> •
    <a href="#-lets-play">Play With It</a> •
    <a href="#-experiment-ideas">Break Things</a>
  </p>
  
  <img src="https://img.shields.io/badge/language-C%2B%2B-blue.svg" alt="Language">
  <img src="https://img.shields.io/badge/network-ENet-brightgreen.svg" alt="Network Library">
  <img src="https://img.shields.io/badge/bugs-probably-red.svg" alt="Bug Status">
  <img src="https://img.shields.io/badge/seriousness-not%20much-yellow.svg" alt="Seriousness Level">
</div>

---

## 📋 About

Welcome to my networking playground! This is where I throw code at the wall and see what sticks. EnetPlayGround is my personal laboratory for experimenting with the ENet reliable UDP networking library. Will it work? Maybe! Will it crash? Probably! Is it fun? I guess.

I actually made this project because I have been playing alot of mmo's and wanted to see how difficult it would be to make a simple mmo game, because every programmer has heard how difficult they are to make. I have no idea what I am doing but I am having fun and learning alot.

## ✨ Cool Stuff I'm Playing With

<table>
  <tr>
    <td width="50%">
      <h3>Server Magic ✨</h3>
      <ul>
        <li>Juggling multiple clients without dropping them</li>
        <li>Events that sometimes fire when they should</li>
        <li>Packets that (usually) arrive in one piece</li>
        <li>Broadcast system that shouts at everyone</li>
      </ul>
    </td>
    <td width="50%">
      <h3>Client Shenanigans 🎭</h3>
      <ul>
        <li>Connection wizardry</li>
        <li>Game state that occasionally syncs</li>
        <li>Logging that tells me what went wrong</li>
        <li>Network knobs to twiddle</li>
      </ul>
    </td>
  </tr>
</table>

## 📸 Screenshots

<div align="center">
  <table>
    <tr>
      <td width="50%">
        <img src="docs/FirstServer.png" alt="Server Console" width="100%" style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.2);">
        <p align="center"><i>Server console showing connected clients</i></p>
      </td>
      <td width="50%">
        <img src="docs/FirstClient.png" alt="Client Interface" width="100%" style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.2);">
        <p align="center"><i>Client interface with messages in chat from another client</i></p>
      </td>
    </tr>
  </table>
</div>

## 🏗️ The Grand Blueprint

```
EnetPlayGround/
├── EnetServer/       # The boss who tells everyone what to do
│   └── src/
│       └── server.cpp   # Where the magic happens (or breaks)
├── EnetClient/       # The minions who follow orders
│   └── src/
│       ├── Constants.h      # Numbers I might change someday
│       ├── EnetClient.cpp   # Network stuff that sends things
│       ├── GameClient.cpp   # Pretending this is a real game
│       └── Logger.h         # For when things go boom
```

## 🛠️ Toys I'm Using

### Libraries
- **ENet**: The networking wizard that somehow makes UDP reliable (still unsure on how udp works tbh)
- **Hello ImGui**: Because command lines are for boomers (jk yall made some cool stuff)

### Package Management
- **vcpkg**: So I don't have to figure out dependencies manually (thank goodness!)

## 🚀 How to Join the Fun

### What You'll Need
- Visual Studio (the newer the better)
- vcpkg (because manual dependency management is no fun)
- A sense of adventure (and patience for debugging)

### Getting Started

1. Grab the code
```bash
git clone https://github.com/alexmollard/EnetPlayGround.git
# Congratulations! You now have my mess on your computer!
```

2. Set up vcpkg (if you haven't already, dont be a bigot)
```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
vcpkg integrate install
# Magic happening...
```

3. Get the goodies
```bash
vcpkg install enet
vcpkg install hello-imgui
# Now you can use the libraries in all visual studio projects without linker hell!
```

4. Fire up Visual Studio
   - Open `EnetServer/EnetServer.sln` for server fun
   - Open `EnetClient/EnetClient.sln` for client adventures
   
5. Hit Build and cross your fingers!
   - Ctrl+Shift+B for the keyboard shortcut enthusiasts

## 🎮 Let's Play!

### Starting the Server
1. Build it
2. Run it
3. Hope it doesn't crash

### Launching Clients
1. Build that too
2. Run as many as your CPU can handle
3. Watch them try to talk to each other
4. Be amazed when it actually works

## 🧪 Experiment Ideas
- What happens if we send a million packets?
- Can the server handle 100 clients? (Probably not, but let's try!)
- How badly can we break the network and still recover?

<!-- Collapsible FAQ Section -->
<br>
<details>
<summary><b>🤔 Frequently Questioned Absurdities</b></summary>

<div class="faq-container" style="padding: 20px; background-color: #f5f5f5; border-radius: 10px;">
  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #ff6b6b; padding-left: 15px;">
    <h4 style="color: #ff6b6b; margin-bottom: 5px;">💬 Why ENet instead of literally anything else?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      Because when I did a 5 second Google search, it was between this and RakNet, and RakNet is dead. Also, I don't know what I'm doing.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #4ecdc4; padding-left: 15px;">
    <h4 style="color: #4ecdc4; margin-bottom: 5px;">🔮 Will this ever be finished?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      <span title="No, but I'll keep adding things until I get distracted by something shiny">Maybe!</span>
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #ff9f1c; padding-left: 15px;">
    <h4 style="color: #ff9f1c; margin-bottom: 5px;">🚀 Can I use this code for my own projects?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      I mean, you <i>could</i>, but why would you <i>want</i> to? That's like choosing to eat off the floor when there's a perfectly good table.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #a463f2; padding-left: 15px;">
    <h4 style="color: #a463f2; margin-bottom: 5px;">🐛 How many bugs are there?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      Too many to count! They've formed their own civilization at this point.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #45b7d1; padding-left: 15px;">
    <h4 style="color: #45b7d1; margin-bottom: 5px;">🧪 Did you test this thoroughly?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      I clicked the "Run" button and it didn't immediately crash. That counts, right?
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #e84855; padding-left: 15px;">
    <h4 style="color: #e84855; margin-bottom: 5px;">🏭 Is this production-ready?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      Only if your production standards include "works on my machine" as the sole acceptance criteria.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #3da35d; padding-left: 15px;">
    <h4 style="color: #3da35d; margin-bottom: 5px;">📝 How's the documentation?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      The code is self-documenting, if you squint hard enough and have a vivid imagination.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #f77f00; padding-left: 15px;">
    <h4 style="color: #f77f00; margin-bottom: 5px;">📊 What's your packet loss rate?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      Let's just say it's more of a "packet donation program" than a reliable network, and I'm yet to add tracking for it.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #9381ff; padding-left: 15px;">
    <h4 style="color: #9381ff; margin-bottom: 5px;">👥 How many concurrent users can it handle?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      At least 2, maybe 3 if the planets align and my CPU isn't busy thinking about other things. It also crashes on a second register, but I'll fix that soon I hope.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #ff5400; padding-left: 15px;">
    <h4 style="color: #ff5400; margin-bottom: 5px;">🗺️ What's your development roadmap?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      Step 1: Make it work. Step 2: Wonder why it works. Step 3: Break it while trying to improve it. Step 4: Repeat.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #ff6392; padding-left: 15px;">
    <h4 style="color: #ff6392; margin-bottom: 5px;">🧠 Why are you like this?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      I think it's because I never learned how to properly use a semicolon. Like, wtf even is that?!
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #03045e; padding-left: 15px;">
    <h4 style="color: #03045e; margin-bottom: 5px;">❓ Can I ask more questions?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      Sure, but I ain't promising answers.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #fb8500; padding-left: 15px;">
    <h4 style="color: #fb8500; margin-bottom: 5px;">👀 Why are you still reading this?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      I have no idea, I thought you would have left by now.
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #bc4749; padding-left: 15px;">
    <h4 style="color: #bc4749; margin-bottom: 5px;">🤕 Are you okay?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      I am not sure, I have been staring at this screen for a while now...
    </div>
  </div>

  <div class="faq-item" style="margin-bottom: 20px; border-left: 4px solid #2b9348; padding-left: 15px;">
    <h4 style="color: #2b9348; margin-bottom: 5px;">🆘 Do you need help?</h4>
    <div style="background-color: #fff; padding: 10px; border-radius: 5px;">
      I am fine, I'm going to go to bed now.
    </div>
  </div>
</div>

</details>

---

<div align="center">
  <br>
  <p><sub>Made with 🍕, and questionable code decisions</sub></p>
  <p><sub>No networks were permanently harmed in the making of this project, I think, I hope, My internet has been playing up since</sub></p>
  <img src="http://estruyf-github.azurewebsites.net/api/VisitorHit?user=alexmollard&repo=EnetPlayGround&countColorcountColor&countColor=%237B1E7B" alt="Visitor Count" />
  <img src="https://media1.tenor.com/m/aGA-AhVPXS0AAAAd/gato-enojado-insano-waza.gif" alt="cat" width="200px">
</div>