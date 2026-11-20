import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import "./themes.css";
import "./app.css";
import "./modern.css";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
