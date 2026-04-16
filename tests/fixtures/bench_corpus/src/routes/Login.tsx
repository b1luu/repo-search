import { signIn } from "../../lib/auth";

export function Login(username: string, password: string) {
  return signIn(username, password);
}
