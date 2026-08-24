import React from "react";
import ReactDOM from "react-dom/client";

import { RunnerApp } from "./RunnerApp";
import "./styles.css";
import "./runner-ux.css";

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <RunnerApp />
  </React.StrictMode>,
);
